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
  elements.connectButton.textContent = connected ? "Disconnect" : "Connect";
  elements.refreshButton.disabled = connected;
  elements.portSelect.disabled = connected;
  elements.forwardSlider.disabled = !connected;
  elements.backwardSlider.disabled = !connected;

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
    applyConfig(Number(configMatch[1]), Number(configMatch[2]));
  }

  if (pendingResponse) {
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
        if (done) break;

        receiveBuffer += decoder.decode(value, { stream: true });
        const lines = receiveBuffer.split("\n");
        receiveBuffer = lines.pop() ?? "";
        lines.forEach(handleSerialLine);
      }
    } catch (error) {
      if (keepReading) {
        setSystemMessage(`Serial read error: ${error.message}`);
      }
    } finally {
      reader.releaseLock();
      reader = null;
    }
  }
}

function executeCommand(command) {
  return new Promise(async (resolve, reject) => {
    if (!writer) {
      reject(new Error("robot is not connected"));
      return;
    }

    const timeout = window.setTimeout(() => {
      pendingResponse = null;
      reject(new Error("robot response timed out"));
    }, COMMAND_TIMEOUT_MS);

    pendingResponse = { resolve, reject, timeout };

    try {
      await writer.write(new TextEncoder().encode(`${command}\n`));
    } catch (error) {
      clearTimeout(timeout);
      pendingResponse = null;
      reject(error);
    }
  });
}

function sendCommand(command) {
  const nextCommand = commandQueue
    .catch(() => undefined)
    .then(() => executeCommand(command));

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
  activePort = selectedPort;
  writer = activePort.writable.getWriter();
  keepReading = true;
  void readLoop();
  setConnectionState(true);

  // ATmega328P boards commonly reset when the serial port opens.
  await new Promise((resolve) => window.setTimeout(resolve, 1500));

  setSystemMessage("Checking firmware…");
  const hello = await sendCommand("HELLO");
  if (!hello.startsWith("OK DEVICE=MAKER_MINI_SUMO")) {
    throw new Error("unsupported device or firmware");
  }

  setSystemMessage("Reading saved alignment…");
  const config = await sendCommand("GET CONFIG");
  if (!config.startsWith("CONFIG ")) {
    throw new Error("invalid configuration response");
  }

  setSystemMessage("Robot connected. Release a slider to save alignment.");
}

async function disconnect() {
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

  if (!activePort) {
    setSaveState(direction, "Not connected", "error");
    setSystemMessage("Connect the robot before saving alignment.");
    return;
  }

  setSaveState(direction, "Saving…");

  try {
    await sendCommand(`SAVE ${code} ${slider.value}`);
    setSaveState(direction, "Saved", "saved");
    setSystemMessage(`${isForward ? "Forward" : "Backward"} alignment saved to EEPROM.`);
  } catch (error) {
    setSaveState(direction, "Save failed", "error");
    setSystemMessage(`Unable to save: ${error.message}`);
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
