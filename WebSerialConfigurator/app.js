const BAUD_RATE = 115200;
const WEBUI_VERSION = "1.3.2";
const COMMAND_TIMEOUT_MS = 2500;
const BOARD_RESET_WAIT_MS = 2000;
const HELLO_ATTEMPTS = 3;

const elements = {
  connectButton: document.querySelector("#connectButton"),
  pageDisconnectButton: document.querySelector("#pageDisconnectButton"),
  browserNotice: document.querySelector("#browserNotice"),
  connectionBadge: document.querySelector("#connectionBadge"),
  connectionText: document.querySelector("#connectionText"),
  systemMessage: document.querySelector("#systemMessage"),
  forwardSlider: document.querySelector("#forwardSlider"),
  backwardSlider: document.querySelector("#backwardSlider"),
  forwardLeftSpeed: document.querySelector("#forwardLeftSpeed"),
  forwardRightSpeed: document.querySelector("#forwardRightSpeed"),
  backwardLeftSpeed: document.querySelector("#backwardLeftSpeed"),
  backwardRightSpeed: document.querySelector("#backwardRightSpeed"),
  forwardSaveState: document.querySelector("#forwardSaveState"),
  backwardSaveState: document.querySelector("#backwardSaveState"),
  rcMapping: document.querySelector("#rcMapping"),
  rcMappingSaveState: document.querySelector("#rcMappingSaveState"),
};

let connectionEpoch = 0;
let firmware = null;
let ready = false;
let sensorBusy = false;
let editorBusy = false;
let currentPage = "home";
let pageLoad = 0;
let activePort = null;
let reader = null;
let writer = null;
let keepReading = false;
let receiveBuffer = "";
let pendingResponse = null;
let commandQueue = Promise.resolve();

document.documentElement.dataset.webuiVersion = WEBUI_VERSION;

function setSystemMessage(message) {
  elements.systemMessage.textContent = message;
}

function setConnectionState(connected) {
  elements.connectionBadge.dataset.state = connected ? "online" : "offline";
  elements.connectionText.textContent = connected ? "Connected" : "Disconnected";
  elements.connectButton.textContent = connected || activePort ? "Disconnect" : "Connect";
  elements.forwardSlider.disabled = !connected;
  elements.backwardSlider.disabled = !connected;
  elements.rcMapping.disabled = !connected || !firmware?.rcMapping;
  document.querySelector("#liveSensors").disabled = !connected || !firmware?.sensor;
  document.querySelector("#firmwareInfo").hidden = !connected;
  document.querySelector("#sessionHelp").hidden = !connected;
  document.querySelector("#sensorPanel").hidden = !connected || currentPage !== "auto";
  renderNav();
  if (!connected) {
    document.querySelector("#sensorData").textContent = "No sensor data received.";
    document.querySelector("#firmwareInfo").textContent = "";
  }

  if (!connected) {
    setSaveState("forward", "Not connected");
    setSaveState("backward", "Not connected");
    elements.rcMappingSaveState.textContent = "Not connected";
    elements.rcMappingSaveState.dataset.state = "";
  }
}

function setSaveState(direction, message, state = "") {
  const target = direction === "forward"
    ? elements.forwardSaveState
    : elements.backwardSaveState;

  target.textContent = message;
  target.dataset.state = state;
}

function updateMotorReadout(direction, rawValue) {
  const trim = Number(rawValue);
  const leftSpeed = trim < 0 ? 100 - Math.abs(trim) : 100;
  const rightSpeed = trim > 0 ? 100 - trim : 100;

  elements[`${direction}LeftSpeed`].textContent = `${leftSpeed}%`;
  elements[`${direction}RightSpeed`].textContent = `${rightSpeed}%`;
}

function applyConfig(forward, backward) {
  elements.forwardSlider.value = String(forward);
  elements.backwardSlider.value = String(backward);
  updateMotorReadout("forward", forward);
  updateMotorReadout("backward", backward);
  setSaveState("forward", "Saved", "saved");
  setSaveState("backward", "Saved", "saved");
}

function handleSerialLine(line) {
  const cleanLine = line.trim();

  if (!cleanLine) {
    return;
  }

  const configMatch = cleanLine.match(/^CONFIG F=(-?\d+) B=(-?\d+)$/);
  if (configMatch) {
    if (Math.abs(Number(configMatch[1])) <= 25 && Math.abs(Number(configMatch[2])) <= 25) {
      applyConfig(Number(configMatch[1]), Number(configMatch[2]));
    }
  }

  if (pendingResponse && (cleanLine.startsWith("ERROR ") || pendingResponse.matches(cleanLine))) {
    const { resolve, reject, timeout } = pendingResponse;
    clearTimeout(timeout);
    pendingResponse = null;

    if (cleanLine.startsWith("ERROR ")) {
      reject(new Error(cleanLine.slice(6).replaceAll("_", " ").toLowerCase()));
    } else {
      resolve(cleanLine);
    }
  }
}

async function readLoop() {
  const decoder = new TextDecoder();

  while (activePort?.readable && keepReading) {
    reader = activePort.readable.getReader();

    try {
      while (keepReading) {
        const { value, done } = await reader.read();
        if (done) {
          if (keepReading) {
            ready = false;
            setConnectionState(false);
            setSystemMessage("Serial connection closed. Disconnect and reconnect to continue.");
            keepReading = false;
          }
          break;
        }

        receiveBuffer += decoder.decode(value, { stream: true });
        const lines = receiveBuffer.split("\n");
        receiveBuffer = lines.pop() ?? "";
        lines.forEach(handleSerialLine);
      }
    } catch (error) {
      if (keepReading) {
        ready = false;
        setConnectionState(false);
        setSystemMessage(`Serial read error: ${error.message}. Reconnect to continue.`);
        keepReading = false;
      }
    } finally {
      reader.releaseLock();
      reader = null;
    }
  }
}

function executeCommand(command, matches) {
  return new Promise(async (resolve, reject) => {
    if (!writer) {
      reject(new Error("robot is not connected"));
      return;
    }

    const timeout = window.setTimeout(() => {
      pendingResponse = null;
      // Only identification is safe to retry while the bootloader is starting.
      if (command !== "HELLO" || ready) {
        ready = false;
        connectionEpoch++;
        setConnectionState(false);
      }
      const error = new Error(`no response to ${command.split(" ").slice(0, 2).join(" ")}; reconnect before continuing`);
      error.code = "RESPONSE_TIMEOUT";
      reject(error);
    }, COMMAND_TIMEOUT_MS);

    pendingResponse = { resolve, reject, timeout, matches };

    try {
      await writer.write(new TextEncoder().encode(`${command}\n`));
    } catch (error) {
      clearTimeout(timeout);
      pendingResponse = null;
      reject(error);
    }
  });
}

function sendCommand(command, matches = responseMatcher(command)) {
  const epoch = connectionEpoch;
  const nextCommand = commandQueue
    .catch(() => undefined)
    .then(() => {
      if (epoch !== connectionEpoch) throw new Error("connection changed; reconnect and retry");
      return executeCommand(command, matches);
    });

  commandQueue = nextCommand.catch(() => undefined);
  return nextCommand;
}

async function connect() {
  const selectedPort = await navigator.serial.requestPort();

  if (!selectedPort) {
    throw new Error("no serial port selected");
  }

  setSystemMessage("Opening serial port…");
  await selectedPort.open({ baudRate: BAUD_RATE });
  receiveBuffer = "";
  activePort = selectedPort;
  writer = activePort.writable.getWriter();
  keepReading = true;
  void readLoop();

  // ATmega328P boards commonly reset when the serial port opens.
  await new Promise((resolve) => window.setTimeout(resolve, BOARD_RESET_WAIT_MS));

  const epoch = connectionEpoch;
  let hello;
  for (let attempt = 1; attempt <= HELLO_ATTEMPTS; attempt++) {
    if (epoch !== connectionEpoch || !keepReading) throw new Error("serial connection closed during startup");
    setSystemMessage(`Checking firmware… (${attempt}/${HELLO_ATTEMPTS})`);
    try {
      hello = await sendCommand("HELLO");
      break;
    } catch (error) {
      if (error.code !== "RESPONSE_TIMEOUT") throw error;
      if (attempt === HELLO_ATTEMPTS) {
        throw new Error("no reply to HELLO after 3 attempts. Check the selected port and uploaded firmware; close other serial applications and try again.");
      }
    }
  }
  if (!hello.startsWith("OK DEVICE=MAKER_MINI_SUMO")) {
    throw new Error("unsupported device or firmware");
  }

  const fields = Object.fromEntries(hello.split(" ").slice(1).map((part) => part.split("=")));
  if (fields.DEVICE !== "MAKER_MINI_SUMO" || ![undefined, "RC", "AutoRC"].includes(fields.FW) ||
      (fields.FW === "AutoRC" && fields.PROTOCOL !== "5") ||
      (fields.FW === "RC" && fields.PROTOCOL !== "2") ||
      (!fields.FW && fields.VERSION !== "1") || !fields.VERSION) throw new Error("unsupported firmware protocol");
  firmware = { type: fields.FW || "RC", version: fields.VERSION, sensor: !!fields.FW, rcMapping: !!fields.FW };
  if (firmware.type === "AutoRC") await sendCommand("CONFIG ON");
  if (firmware.rcMapping) {
    setSystemMessage("Reading RC channel mapping…");
    const rcConfig = await sendCommand("GET RC");
    const rcMatch = rcConfig.match(/^RC MAP=([01])$/);
    if (!rcMatch) throw new Error("invalid RC channel mapping response");
    elements.rcMapping.value = rcMatch[1];
    elements.rcMappingSaveState.textContent = "Saved";
    elements.rcMappingSaveState.dataset.state = "saved";
  } else {
    elements.rcMapping.value = "0";
    elements.rcMappingSaveState.textContent = "Update firmware to change mapping";
    elements.rcMappingSaveState.dataset.state = "";
  }
  document.querySelector("#firmwareInfo").textContent = `MakerMiniSumo_${firmware.type} · Version ${firmware.version}`;
  document.querySelector("#alignmentHelp").textContent = firmware.type === "AutoRC"
    ? "Release a slider to save. Disconnect USB and reset the robot before testing alignment."
    : "Move the robot with its RC remote, then release a slider to save.";
  document.querySelector("#sessionHelp").textContent = firmware.type === "AutoRC"
    ? "Motors locked for configuration. After saving, disconnect USB and reset with START released / IR at STOP."
    : "RC remains active. Use the remote to test alignment. Auto pages require AutoRC firmware.";
  setSystemMessage("Reading saved alignment…");
  const config = await sendCommand("GET CONFIG");
  if (!config.startsWith("CONFIG ") || config.slice(7).split(" ").some(part => Math.abs(Number(part.split("=")[1])) > 25)) {
    throw new Error("invalid configuration response");
  }

  ready = true;
  setConnectionState(true);
  setSystemMessage("Robot connected. Release a slider to save alignment.");
}

async function disconnect() {
  connectionEpoch++;
  ready = false;
  pageLoad++;
  currentPage = "home";
  document.querySelector("#homePage").hidden = false;
  document.querySelector("#editorPage").hidden = true;
  document.querySelector("#liveSensors").checked = false;
  keepReading = false;

  if (pendingResponse) {
    clearTimeout(pendingResponse.timeout);
    pendingResponse.reject(new Error("robot disconnected"));
    pendingResponse = null;
  }

  if (reader) {
    await reader.cancel();
  }

  if (writer) {
    writer.releaseLock();
    writer = null;
  }

  if (activePort) {
    await activePort.close();
    activePort = null;
  }

  commandQueue = Promise.resolve();
  setConnectionState(false);
  setSystemMessage("Robot disconnected.");
}

async function saveSlider(direction) {
  const isForward = direction === "forward";
  const slider = isForward ? elements.forwardSlider : elements.backwardSlider;
  const code = isForward ? "F" : "B";

  if (!ready) {
    setSaveState(direction, "Not connected", "error");
    setSystemMessage("Connect the robot before saving alignment.");
    return;
  }

  slider.disabled = true;
  setSaveState(direction, "Saving…");

  try {
    const value = slider.value;
    await sendCommand(`SAVE ${code} ${value}`);
    setSaveState(direction, "Saved", "saved");
    setSystemMessage(`${isForward ? "Forward" : "Backward"} alignment saved to EEPROM.`);
  } catch (error) {
    setSaveState(direction, "Save failed", "error");
    setSystemMessage(`Unable to save: ${error.message}`);
  } finally {
    slider.disabled = !ready;
  }
}

elements.connectButton.addEventListener("click", async () => {
  elements.connectButton.disabled = true;

  try {
    if (activePort) {
      await disconnect();
    } else {
      await connect();
    }
  } catch (error) {
    if (activePort) await disconnect();
    setSystemMessage(`Connection failed: ${error.message}`);
  } finally {
    elements.connectButton.disabled = false;
  }
});

elements.pageDisconnectButton.addEventListener("click", async () => {
  elements.pageDisconnectButton.disabled = true;
  try { await disconnect(); }
  catch (error) { setSystemMessage(`Unable to disconnect: ${error.message}`); }
  finally { elements.pageDisconnectButton.disabled = false; }
});

elements.forwardSlider.addEventListener("input", (event) => {
  updateMotorReadout("forward", event.target.value);
  setSaveState("forward", activePort ? "Release to save" : "Not connected");
});

elements.backwardSlider.addEventListener("input", (event) => {
  updateMotorReadout("backward", event.target.value);
  setSaveState("backward", activePort ? "Release to save" : "Not connected");
});

elements.forwardSlider.addEventListener("change", () => saveSlider("forward"));
elements.backwardSlider.addEventListener("change", () => saveSlider("backward"));
elements.rcMapping.addEventListener("change", async () => {
  if (!ready) return;
  const value = elements.rcMapping.value;
  elements.rcMapping.disabled = true;
  elements.rcMappingSaveState.textContent = "Saving…";
  elements.rcMappingSaveState.dataset.state = "";
  try {
    await sendCommand(`SAVE RC ${value}`);
    elements.rcMappingSaveState.textContent = "Saved";
    elements.rcMappingSaveState.dataset.state = "saved";
    setSystemMessage("RC channel mapping saved to EEPROM.");
  } catch (error) {
    elements.rcMappingSaveState.textContent = "Save failed";
    elements.rcMappingSaveState.dataset.state = "error";
    setSystemMessage(`Unable to save RC mapping: ${error.message}`);
  } finally {
    elements.rcMapping.disabled = !ready || !firmware?.rcMapping;
  }
});

if (!("serial" in navigator)) {
  elements.browserNotice.hidden = false;
  elements.connectButton.disabled = true;
  setSystemMessage("Web Serial is not supported in this browser.");
} else {
  navigator.serial.addEventListener("disconnect", async (event) => {
    if (event.target === activePort) await disconnect();
  });
}

setConnectionState(false);
updateMotorReadout("forward", elements.forwardSlider.value);
updateMotorReadout("backward", elements.backwardSlider.value);

function responseMatcher(command) {
  if (command === "HELLO") return line => line.startsWith("OK DEVICE=");
  if (command === "CONFIG ON") return line => line === "OK CONFIG";
  if (command === "GET CONFIG") return line => /^CONFIG F=-?\d+ B=-?\d+$/.test(line);
  if (command === "GET SENSOR") return line => /^SENSOR( -?\d+(\.\d+)?){8}$/.test(line);
  if (command === "GET RC") return line => /^RC MAP=[01]$/.test(line);
  if (command.startsWith("GET ")) return line => line.startsWith(command.slice(4) + " ");
  if (/^SAVE [FB] /.test(command)) {
    const [, direction, value] = command.split(" ");
    return line => line === `OK SAVED ${direction}=${value}`;
  }
  if (/^SAVE RC [01]$/.test(command)) {
    const value = command.at(-1);
    return line => line === `OK SAVED RC=${value}`;
  }
  const parts = command.split(" ");
  const expected = `OK SAVED ${parts[1]}${parts[1] === "STR" ? ` ${parts[2]}` : ""}`;
  return line => line === expected;
}
function renderNav() {
  const nav = document.querySelector("#pageNav");
  nav.replaceChildren();
  const labels = ["Main", "Auto Routine", "Strategy LLL", "Strategy LLH", "Strategy LHL", "Strategy LHH", "Strategy HLL", "Strategy HLH · Defense", "Strategy HHL"];
  labels.forEach((label, index) => {
    const page = index === 0 ? "home" : index === 1 ? "auto" : String(index-2);
    const button = document.createElement("button");
    button.className = "button button-secondary";
    button.textContent = label;
    button.setAttribute("aria-current", currentPage === page ? "page" : "false");
    button.disabled = editorBusy || (index > 0 && (!ready || firmware?.type !== "AutoRC"));
    button.addEventListener("click", () => showPage(page));
    nav.append(button);
  });
}
const autoGroups = [
  ["Search", [
    ["Motor left (%)",-100,100], ["Motor right (%)",-100,100],
  ]],
  ["Backoff", [
    ["Reverse speed (%)",0,100], ["Reverse duration (ms)",0,10000],
    ["Turn speed (%)",0,100], ["Turn duration (ms)",0,10000],
  ]],
  ["Attack", [
    ["Initial speed (%)",0,100], ["Ramp time (ms)",0,10000],
    ["Maximum speed (%)",0,100],
  ]],
];
const autoFields = autoGroups.flatMap(([, fields]) => fields);
const defenseFields = [
  ["Forward repetitions",0,100], ["Wait between moves (ms)",1,10000],
  ["Forward left (%)",0,100], ["Forward right (%)",0,100], ["Forward duration (ms)",1,10000],
];
function numberField(label, value, min, max) {
  const wrapper = document.createElement("label");
  wrapper.className = "tuning-field";
  const span = document.createElement("span"); span.textContent = label;
  const input = document.createElement("input");
  input.type = "number"; input.min = min; input.max = max; input.step = 1;
  input.required = true; input.value = value;
  wrapper.append(span,input); return wrapper;
}
async function showPage(page) {
  if (editorBusy) return;
  const home = document.querySelector("#homePage");
  const editorPage = document.querySelector("#editorPage");
  const editor = document.querySelector("#editorContent");
  const load = ++pageLoad;
  currentPage = page; renderNav();
  home.hidden = page !== "home"; editorPage.hidden = page === "home";
  document.querySelector("#sensorPanel").hidden = page !== "auto" || !ready;
  if (page === "home") return;
  editor.textContent = "Reading saved settings…";
  const group = page === "auto" ? "AUTO" : page === "5" ? "DEF" : `STR ${page}`;
  try {
    const line = await sendCommand(`GET ${group}`);
    if (load !== pageLoad || !ready) return;
    const values = line.slice(group.length+1).split(" ").map(Number);
    const fields = page === "auto" ? autoFields : page === "5" ? defenseFields : null;
    if (values.length !== (fields ? fields.length : 20) || values.some(v => !Number.isInteger(v))) throw new Error("invalid settings response");
    editor.replaceChildren();
    const heading = document.createElement("h2");
    const dip = Number(page).toString(2).padStart(3,"0").replaceAll("0","L").replaceAll("1","H");
    heading.textContent = page === "auto" ? "Auto Routine" : `Strategy ${dip}${page === "5" ? " · Defense" : ""}`;
    const help = document.createElement("p"); help.className = "field-help";
    help.textContent = page === "auto" ? "Search defaults to straight. Speeds are adjusted by saved motor alignment."
      : page === "5" ? "Exit when repetitions finish or any opponent sensor detects a target. Zero repetitions skips defense."
      : "Enabled rows run from top to bottom, then search/attack starts. Positive = forward; negative = reverse; zero = stop.";
    const form = document.createElement("form");
    const inputs = [];
    if (page === "auto") {
      let valueIndex = 0;
      autoGroups.forEach(([title, groupFields]) => {
        const group = document.createElement("fieldset"); group.className = "routine-group";
        const legend = document.createElement("legend"); legend.textContent = title;
        const grid = document.createElement("div"); grid.className = "tuning-grid";
        groupFields.forEach(([label,min,max]) => {
          const field = numberField(label,values[valueIndex++],min,max);
          inputs.push(field.querySelector("input")); grid.append(field);
        });
        group.append(legend,grid); form.append(group);
      });
    } else if (fields) {
      const grid = document.createElement("div"); grid.className = "tuning-grid";
      fields.forEach(([label,min,max],i) => {
        const field = numberField(label,values[i],min,max); inputs.push(field.querySelector("input")); grid.append(field);
      }); form.append(grid);
    } else {
      for (let row = 0; row < 5; row++) {
        const card = document.createElement("fieldset"); card.className = "strategy-row";
        const legend = document.createElement("legend"); legend.textContent = `Step ${row+1}`;
        const toggleLabel = document.createElement("label");
        const toggle = document.createElement("input"); toggle.type = "checkbox"; toggle.checked = !!values[row*4];
        toggleLabel.append(toggle, " Enabled"); card.append(legend,toggleLabel); inputs.push(toggle);
        const rowInputs = [];
        [["Left motor (%)",-100,100],["Right motor (%)",-100,100],["Duration (ms)",1,10000]].forEach(([label,min,max],col) => {
          const field = numberField(label,values[row*4+col+1],min,max);
          const input = field.querySelector("input"); input.disabled = !toggle.checked;
          rowInputs.push(input); inputs.push(input); card.append(field);
        });
        toggle.addEventListener("change", () => rowInputs.forEach(input => input.disabled = !toggle.checked));
        form.append(card);
      }
    }
    const save = document.createElement("button"); save.type = "submit";
    save.className = "button button-primary"; save.textContent = fields ? "Save settings" : "Save strategy";
    const status = document.createElement("p"); status.setAttribute("role","status"); status.textContent = "Read from robot";
    form.addEventListener("input", () => status.textContent = "Unsaved changes");
    form.append(save,status);
    form.addEventListener("submit", async event => {
      event.preventDefault();
      if (!ready || editorBusy || !form.reportValidity()) return;
      const data = inputs.map(input => input.type === "checkbox" ? Number(input.checked) : Number(input.value));
      if (data.some((v,i) => !Number.isInteger(v) || (inputs[i].type !== "checkbox" && (v < Number(inputs[i].min) || v > Number(inputs[i].max))))) {
        status.textContent = "Check every row: values must be integers within the displayed limits."; return;
      }
      editorBusy = true; renderNav();
      const disabled = inputs.map(input => input.disabled);
      inputs.forEach(input => input.disabled = true); save.disabled = true;
      status.textContent = "Saving…";
      try {
        await sendCommand(`SAVE ${group} ${data.join(" ")}`);
        status.textContent = "Saved to EEPROM";
      } catch (error) { status.textContent = `Save failed: ${error.message}`; }
      finally {
        editorBusy = false; inputs.forEach((input,i) => input.disabled = disabled[i]); save.disabled = !ready; renderNav();
      }
    });
    editor.append(heading,help,form);
  } catch (error) {
    if (load === pageLoad) editor.textContent = `Unable to load settings: ${error.message}. Select this page to retry.`;
  }
}
window.setInterval(async () => {
  if (!ready || sensorBusy || editorBusy || pendingResponse || !document.querySelector("#liveSensors").checked || currentPage !== "auto") return;
  sensorBusy = true;
  try {
    const line = await sendCommand("GET SENSOR");
    if (!ready || currentPage !== "auto") return;
    const [mask,left,right,pot,start,dip,,battery] = line.slice(7).split(" ").map(Number);
    const sensitivity = Math.floor(25 + pot * 50 / 1023);
    const target = document.querySelector("#sensorData"); target.replaceChildren();
    const rows = [
      ...["Left","Front left","Front centre","Front right","Right"].map((name,i) => [name, mask & (1<<i) ? "Detected" : "Clear"]),
      ["Edge left raw ADC",left], ["Edge right raw ADC",right],
      ["Trim raw ADC",pot], ["IR sensitivity",`${sensitivity}%`],
      ["START / IR D2",start ? "HIGH" : "LOW"],
      ["DIP",dip.toString(2).padStart(3,"0").replaceAll("0","L").replaceAll("1","H")],
      ["Battery",`${battery.toFixed(2)} V`],
    ];
    rows.forEach(([label,value]) => {
      const item = document.createElement("div"); const title = document.createElement("span");
      title.textContent = label; const reading = document.createElement("strong"); reading.textContent = value;
      item.append(title,reading); target.append(item);
    });
  } catch (error) { document.querySelector("#sensorData").textContent = `Sensor data unavailable: ${error.message}`; }
  finally { sensorBusy = false; }
},250);
