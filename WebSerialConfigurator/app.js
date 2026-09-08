const BAUD_RATE = 115200;
const NEW_PORT_VALUE = "new";
const COMMAND_TIMEOUT_MS = 2500;

const elements = {
  portSelect: document.querySelector("#portSelect"),
  refreshButton: document.querySelector("#refreshButton"),
  connectButton: document.querySelector("#connectButton"),
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
};

let connectionEpoch = 0;
let firmware = null;
let ready = false;
let sensorBusy = false;
let editorBusy = false;
let currentPage = "home";
let pageLoad = 0;
let approvedPorts = [];
let activePort = null;
let reader = null;
let writer = null;
let keepReading = false;
let receiveBuffer = "";
let pendingResponse = null;
let commandQueue = Promise.resolve();

function setSystemMessage(message) {
  elements.systemMessage.textContent = message;
}

function setConnectionState(connected) {
  elements.connectionBadge.dataset.state = connected ? "online" : "offline";
  elements.connectionText.textContent = connected ? "Connected" : "Disconnected";
  elements.connectButton.textContent = connected || activePort ? "Disconnect" : "Connect";
  elements.refreshButton.disabled = connected;
  elements.portSelect.disabled = connected;
  elements.forwardSlider.disabled = !connected;
  elements.backwardSlider.disabled = !connected;
  document.querySelector("#liveSensors").disabled = !connected || !firmware?.sensor;
  renderNav();
  if (!connected) {
    document.querySelector("#sensorData").textContent = "No sensor data received.";
    document.querySelector("#firmwareInfo").textContent = "Not connected";
  }

  if (!connected) {
    setSaveState("forward", "Not connected");
    setSaveState("backward", "Not connected");
  }
}

function setSaveState(direction, message, state = "") {
  const target = direction === "forward"
    ? elements.forwardSaveState
    : elements.backwardSaveState;

  target.textContent = message;
  target.dataset.state = state;
}

function formatPort(port, index) {
  const info = port.getInfo();
  const vendor = info.usbVendorId
    ? `VID ${info.usbVendorId.toString(16).toUpperCase().padStart(4, "0")}`
    : "Serial";
  const product = info.usbProductId
    ? `PID ${info.usbProductId.toString(16).toUpperCase().padStart(4, "0")}`
    : `Port ${index + 1}`;

  return `${vendor} · ${product}`;
}

async function refreshPorts(selectedPort = null) {
  if (!("serial" in navigator)) {
    return;
  }

  approvedPorts = await navigator.serial.getPorts();
  elements.portSelect.replaceChildren();

  approvedPorts.forEach((port, index) => {
    const option = document.createElement("option");
    option.value = String(index);
    option.textContent = formatPort(port, index);
    option.selected = port === selectedPort;
    elements.portSelect.append(option);
  });

  const newPortOption = document.createElement("option");
  newPortOption.value = NEW_PORT_VALUE;
  newPortOption.textContent = "Select new port…";
  newPortOption.selected = !selectedPort && approvedPorts.length === 0;
  elements.portSelect.append(newPortOption);

  if (!selectedPort && approvedPorts.length > 0) {
    elements.portSelect.value = "0";
  }

  setSystemMessage(
    approvedPorts.length > 0
      ? `${approvedPorts.length} approved serial port${approvedPorts.length === 1 ? "" : "s"} found.`
      : "No approved ports yet. Select a new port to continue.",
  );
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
      ready = false;
      connectionEpoch++;
      setConnectionState(false);
      reject(new Error("robot response timed out; reconnect before continuing"));
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
  let selectedPort;

  if (elements.portSelect.value === NEW_PORT_VALUE) {
    selectedPort = await navigator.serial.requestPort();
    await refreshPorts(selectedPort);
  } else {
    selectedPort = approvedPorts[Number(elements.portSelect.value)];
  }

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
  await new Promise((resolve) => window.setTimeout(resolve, 1500));

  setSystemMessage("Checking firmware…");
  const hello = await sendCommand("HELLO");
  if (!hello.startsWith("OK DEVICE=MAKER_MINI_SUMO")) {
    throw new Error("unsupported device or firmware");
  }

  const fields = Object.fromEntries(hello.split(" ").slice(1).map((part) => part.split("=")));
  if (fields.DEVICE !== "MAKER_MINI_SUMO" || ![undefined, "RC", "AutoRC"].includes(fields.FW) ||
      (fields.FW === "AutoRC" && fields.PROTOCOL !== "2") ||
      (fields.FW === "RC" && fields.PROTOCOL !== "1") ||
      (!fields.FW && fields.VERSION !== "1") || !fields.VERSION) throw new Error("unsupported firmware protocol");
  firmware = { type: fields.FW || "RC", version: fields.VERSION, sensor: !!fields.FW };
  if (firmware.type === "AutoRC") await sendCommand("CONFIG ON");
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

elements.refreshButton.addEventListener("click", async () => {
  try {
    await refreshPorts();
  } catch (error) {
    setSystemMessage(`Unable to refresh ports: ${error.message}`);
  }
});

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

if (!("serial" in navigator)) {
  elements.browserNotice.hidden = false;
  elements.refreshButton.disabled = true;
  elements.connectButton.disabled = true;
  setSystemMessage("Web Serial is not supported in this browser.");
} else {
  navigator.serial.addEventListener("connect", () => refreshPorts());
  navigator.serial.addEventListener("disconnect", async (event) => {
    if (event.target === activePort) {
      await disconnect();
    } else {
      await refreshPorts();
    }
  });

  refreshPorts();
}

setConnectionState(false);
updateMotorReadout("forward", elements.forwardSlider.value);
updateMotorReadout("backward", elements.backwardSlider.value);

function responseMatcher(command) {
  if (command === "HELLO") return line => line.startsWith("OK DEVICE=");
  if (command === "CONFIG ON") return line => line === "OK CONFIG";
  if (command === "GET CONFIG") return line => /^CONFIG F=-?\d+ B=-?\d+$/.test(line);
  if (command === "GET SENSOR") return line => /^SENSOR( -?\d+(\.\d+)?){7}$/.test(line);
  if (command.startsWith("GET ")) return line => line.startsWith(command.slice(4) + " ");
  if (/^SAVE [FB] /.test(command)) {
    const [, direction, value] = command.split(" ");
    return line => line === `OK SAVED ${direction}=${value}`;
  }
  const parts = command.split(" ");
  const expected = `OK SAVED ${parts[1]}${parts[1] === "STR" ? ` ${parts[2]}` : ""}`;
  return line => line === expected;
}
function renderNav() {
  const nav = document.querySelector("#pageNav");
  nav.replaceChildren();
  const labels = ["Utama", "Auto behaviour", "Strategy LLL", "Strategy LLH", "Strategy LHL", "Strategy LHH", "Strategy HLL", "Strategy HLH · Defense", "Strategy HHL"];
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
const autoFields = [
  ["Search left (%)",-100,100], ["Search right (%)",-100,100],
  ["Attack initial speed (%)",0,100], ["Attack maximum speed (%)",0,100],
  ["Attack ramp time (ms)",0,10000], ["Backoff reverse speed (%)",0,100],
  ["Backoff reverse duration (ms)",0,10000], ["Backoff turn speed (%)",0,100],
  ["Backoff turn duration (ms)",0,10000], ["Backoff pause (ms)",0,10000],
  ["Edge threshold (% of starting surface reading)",1,99],
];
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
  const editor = document.querySelector("#editorPage");
  const load = ++pageLoad;
  currentPage = page; renderNav();
  home.hidden = page !== "home"; editor.hidden = page === "home";
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
    heading.textContent = page === "auto" ? "Auto behaviour" : `Strategy ${dip}${page === "5" ? " · Defense" : ""}`;
    const help = document.createElement("p"); help.className = "field-help";
    help.textContent = page === "auto" ? "Search defaults to straight. Speeds are adjusted by saved motor alignment."
      : page === "5" ? "Exit when repetitions finish or any opponent sensor detects a target. Zero repetitions skips defense."
      : "Enabled rows run from top to bottom, then search/attack starts. Positive = forward; negative = reverse; zero = stop.";
    const form = document.createElement("form");
    const inputs = [];
    if (fields) {
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
  if (!ready || sensorBusy || editorBusy || pendingResponse || !document.querySelector("#liveSensors").checked || currentPage !== "home") return;
  sensorBusy = true;
  try {
    const line = await sendCommand("GET SENSOR");
    if (!ready) return;
    const [mask,left,right,start,dip,state,battery] = line.slice(7).split(" ").map(Number);
    const target = document.querySelector("#sensorData"); target.replaceChildren();
    const rows = [
      ...["Left","Front left","Front centre","Front right","Right"].map((name,i) => [name, mask & (1<<i) ? "Detected" : "Clear"]),
      ["Edge left",left], ["Edge right",right], ["START / IR D2",start ? "HIGH" : "LOW"],
      ["DIP",dip.toString(2).padStart(3,"0").replaceAll("0","L").replaceAll("1","H")],
      ["State",state === 99 ? "Configuration" : state === 98 ? "RC" : String(state)], ["Battery",`${battery.toFixed(2)} V`],
    ];
    rows.forEach(([label,value]) => {
      const item = document.createElement("div"); const title = document.createElement("span");
      title.textContent = label; const reading = document.createElement("strong"); reading.textContent = value;
      item.append(title,reading); target.append(item);
    });
  } catch (error) { document.querySelector("#sensorData").textContent = `Sensor data unavailable: ${error.message}`; }
  finally { sensorBusy = false; }
},250);
