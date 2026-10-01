// HT5023 VS Code extension.
//
// Written in plain JavaScript on purpose: no TypeScript, no bundler, no npm
// install. The folder can be copied straight into ~/.vscode/extensions and
// works after a window reload, which matters for an SDK that people clone
// rather than install from a marketplace.
//
// Three project shapes are supported, detected rather than configured:
//
//   platformio.ini            -> delegate to PlatformIO (`pio run`, `-t upload`)
//   Makefile + sdk/examples/  -> the SDK's own build (`make EXAMPLE=<name>`)
//   a bare .ino               -> arduino-cli against the HT5023 core
//
// Everything that touches the chip goes through the SDK's Python tools, which
// are the same ones the Flasher GUI and the Arduino uploader use.

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const cp = require('child_process');

const TASK_TYPE = 'ht5023';
let statusItems = [];

// ---------------------------------------------------------------- helpers --
function cfg() {
  return vscode.workspace.getConfiguration('ht5023');
}

function workspaceRoot() {
  const folders = vscode.workspace.workspaceFolders;
  return folders && folders.length ? folders[0].uri.fsPath : undefined;
}

function exists(p) {
  try { fs.accessSync(p); return true; } catch (_) { return false; }
}

/** Quote a path for a shell command line. */
function q(p) {
  return /[\s"']/.test(p) ? `"${p}"` : p;
}

/**
 * The SDK checkout: the folder holding the Makefile plus sdk/ or examples/.
 * Searched from the setting, then upwards from the workspace, then upwards
 * from this extension (which lives inside the SDK when installed by copy).
 */
function findSdk() {
  const configured = cfg().get('sdkPath');
  if (configured && exists(path.join(configured, 'Makefile'))) return configured;

  const looksRight = (d) =>
    exists(path.join(d, 'Makefile')) &&
    (exists(path.join(d, 'sdk')) || exists(path.join(d, 'examples')));

  const walkUp = (start) => {
    let d = start;
    for (let i = 0; i < 8 && d; i++) {
      if (looksRight(d)) return d;
      const up = path.dirname(d);
      if (up === d) break;
      d = up;
    }
    return undefined;
  };

  return walkUp(workspaceRoot() || '') || walkUp(__dirname);
}

/** 'pio' | 'sdk' | 'sketch' | undefined */
function projectKind() {
  const root = workspaceRoot();
  if (!root) return undefined;
  if (exists(path.join(root, 'platformio.ini'))) return 'pio';
  if (exists(path.join(root, 'Makefile')) &&
      (exists(path.join(root, 'sdk')) || exists(path.join(root, 'examples')))) return 'sdk';
  const active = vscode.window.activeTextEditor;
  if (active && active.document.fileName.toLowerCase().endsWith('.ino')) return 'sketch';
  if (findSdk()) return 'sdk';
  return undefined;
}

function which(cmd) {
  try {
    const out = cp.execSync(
      process.platform === 'win32' ? `where ${cmd}` : `command -v ${cmd}`,
      { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim();
    return out.split(/\r?\n/)[0] || undefined;
  } catch (_) { return undefined; }
}

/**
 * Serial ports, best candidates first. Read from /dev rather than shelling out
 * so the status bar stays responsive; Windows has no equivalent listing, so
 * there the port has to be set (or PlatformIO asked).
 */
function listSerialPorts() {
  if (process.platform === 'win32') return [];
  const out = [];
  const byId = '/dev/serial/by-id';
  if (exists(byId)) {
    for (const name of fs.readdirSync(byId)) {
      try { out.push(fs.realpathSync(path.join(byId, name))); } catch (_) { /* gone */ }
    }
  }
  if (exists('/dev')) {
    for (const name of fs.readdirSync('/dev')) {
      if (/^tty(USB|ACM)\d+$/.test(name)) {
        const full = path.join('/dev', name);
        if (!out.includes(full)) out.push(full);
      }
    }
  }
  return out.sort();
}

function serialPort() {
  const configured = cfg().get('serialPort');
  if (configured) return configured;
  const found = listSerialPorts();
  return found.length ? found[0] : '';
}

/** Run a shell command as a VS Code task, so output lands in the terminal. */
function runTask(name, command, cwd, matcher) {
  const task = new vscode.Task(
    { type: TASK_TYPE, task: name },
    vscode.TaskScope.Workspace,
    name,
    'HT5023',
    new vscode.ShellExecution(command, { cwd }),
    matcher || []
  );
  task.presentationOptions = {
    reveal: vscode.TaskRevealKind.Always,
    panel: vscode.TaskPanelKind.Dedicated,
    clear: true,
  };
  return vscode.tasks.executeTask(task);
}

function fail(msg) {
  vscode.window.showErrorMessage(`HT5023: ${msg}`);
}

// ------------------------------------------------------------- operations --
function doBuild() {
  const kind = projectKind();
  const root = workspaceRoot();

  if (kind === 'pio') {
    if (!which('pio') && !which('platformio')) {
      return fail('PlatformIO CLI not found. Install the PlatformIO IDE extension, or open an SDK folder instead.');
    }
    return runTask('build', 'pio run', root, ['$gcc']);
  }

  if (kind === 'sketch') {
    const cli = which('arduino-cli');
    if (!cli) return fail('arduino-cli not found. Run ./install-linux.sh in the SDK, or put arduino-cli on PATH.');
    const file = vscode.window.activeTextEditor.document.fileName;
    const folder = path.dirname(file);
    if (path.basename(folder) !== path.basename(file, '.ino')) {
      return fail(`A sketch must sit in a folder with the same name — move ${path.basename(file)} into a folder called '${path.basename(file, '.ino')}'.`);
    }
    return runTask('build', `arduino-cli compile -b ht5023:ht5023:ht5023 ${q(folder)}`, path.dirname(folder), ['$gcc']);
  }

  const sdk = findSdk();
  if (!sdk) return fail('No HT5023 project found. Open the SDK folder, or a PlatformIO project with board = ht5023.');
  const example = cfg().get('example') || 'blinky';
  return runTask('build', `make build EXAMPLE=${example}`, sdk, ['$gcc']);
}

function doUpload(viaUsb) {
  const kind = projectKind();
  const root = workspaceRoot();
  const py = cfg().get('pythonPath') || 'python3';
  const sdk = findSdk();

  if (kind === 'pio') {
    // The protocol comes from platformio.ini -- PlatformIO has no command-line
    // override for it -- so a USB upload only adds the port, and says what the
    // .ini still has to declare.
    let cmd = 'pio run -t upload';
    if (viaUsb) {
      const port = serialPort();
      if (!port) return fail('No serial port found for a USB upload. Plug in the adapter, or set ht5023.serialPort.');
      cmd += ` --upload-port ${q(port)}`;
      vscode.window.showInformationMessage(
        `HT5023: uploading over ${port}. platformio.ini needs upload_protocol = bootloader for this to go through the bootloader.`);
    }
    return runTask('upload', cmd, root, []);
  }

  if (!sdk) return fail('No HT5023 SDK found — set ht5023.sdkPath.');

  if (kind === 'sketch') {
    const file = vscode.window.activeTextEditor.document.fileName;
    const folder = path.dirname(file);
    const layout = viaUsb ? 'bootloader' : 'standalone';
    const out = path.join(sdk, 'build', 'ino');
    const bin = path.join(out, `${path.basename(folder)}.ino.bin`);
    const uploader = path.join(sdk, 'arduino', 'hardware', 'ht5023', 'ht5023', 'tools', 'ht5023_upload.py');
    const method = viaUsb ? 'usb' : 'jlink';
    let cmd = `arduino-cli compile -b ht5023:ht5023:ht5023:layout=${layout} --output-dir ${q(out)} ${q(folder)}` +
              ` && ${q(py)} ${q(uploader)} --method ${method} --bin ${q(bin)}`;
    if (viaUsb) {
      const port = serialPort();
      if (!port) return fail('No serial port found. Plug in the USB-UART adapter.');
      cmd += ` --port ${q(port)} --baud ${cfg().get('serialBaud') || 115200}`;
    } else {
      cmd += ` --speed ${cfg().get('jlinkSpeed') || 1000}`;
    }
    return runTask('upload', cmd, sdk, ['$gcc']);
  }

  // SDK / Makefile project.
  const example = cfg().get('example') || 'blinky';
  if (viaUsb) {
    const port = serialPort();
    if (!port) return fail('No serial port found. Plug in the USB-UART adapter.');
    const tool = path.join(sdk, 'tools', 'ht5023_uart_flash.py');
    const cmd = `make build EXAMPLE=${example} BOOT_APP=1` +
                ` && ${q(py)} ${q(tool)} --port ${q(port)} --file ${q(path.join(sdk, 'build', 'app.bin'))}`;
    return runTask('upload', cmd, sdk, ['$gcc']);
  }
  const tool = path.join(sdk, 'tools', 'ht5023_flash_seq.py');
  const cmd = `make build EXAMPLE=${example}` +
              ` && ${q(py)} ${q(tool)} ${q(path.join(sdk, 'build', 'app.bin'))} --speed ${cfg().get('jlinkSpeed') || 1000}`;
  return runTask('upload', cmd, sdk, ['$gcc']);
}

function doMonitor() {
  const port = serialPort();
  if (!port) {
    return fail(process.platform === 'win32'
      ? 'Set ht5023.serialPort (e.g. COM11) — Windows has no port listing here.'
      : 'No serial port found. Plug in the USB-UART adapter on PC11/PC12.');
  }
  const baud = cfg().get('serialBaud') || 115200;

  // pio's monitor is the nicest when it is there; miniterm ships with pyserial,
  // which the SDK already requires, so it is always a usable fallback.
  const py = cfg().get('pythonPath') || 'python3';
  const cmd = which('pio')
    ? `pio device monitor --port ${q(port)} --baud ${baud}`
    : `${q(py)} -m serial.tools.miniterm ${q(port)} ${baud}`;

  const term = vscode.window.createTerminal({ name: `HT5023 ${path.basename(port)}` });
  term.show();
  term.sendText(cmd);
}

function doClean() {
  const kind = projectKind();
  if (kind === 'pio') return runTask('clean', 'pio run -t clean', workspaceRoot(), []);
  const sdk = findSdk();
  if (!sdk) return fail('No HT5023 SDK found.');
  return runTask('clean', 'make clean', sdk, []);
}

async function doSelectExample() {
  const sdk = findSdk();
  if (!sdk) return fail('No HT5023 SDK found — set ht5023.sdkPath.');
  let names = [];
  try {
    names = cp.execSync('make list-examples', { cwd: sdk }).toString().trim().split(/\s+/);
  } catch (e) {
    return fail(`Could not list examples: ${e.message}`);
  }
  const picked = await vscode.window.showQuickPick(names, {
    placeHolder: 'Which example should Build and Upload use?',
  });
  if (!picked) return;
  await cfg().update('example', picked, vscode.ConfigurationTarget.Workspace);
  refreshStatus();
}

async function doSelectPort() {
  const ports = listSerialPorts();
  if (!ports.length) {
    const typed = await vscode.window.showInputBox({
      prompt: 'No ports detected. Enter one by hand (e.g. /dev/ttyUSB0 or COM11)',
    });
    if (typed) {
      await cfg().update('serialPort', typed, vscode.ConfigurationTarget.Workspace);
      refreshStatus();
    }
    return;
  }
  const picked = await vscode.window.showQuickPick([...ports, '(auto-detect)'], {
    placeHolder: 'Serial port for the monitor and USB uploads',
  });
  if (!picked) return;
  await cfg().update('serialPort', picked === '(auto-detect)' ? '' : picked,
                     vscode.ConfigurationTarget.Workspace);
  refreshStatus();
}

async function doInstallBootloader() {
  const sdk = findSdk();
  if (!sdk) return fail('No HT5023 SDK found.');
  const ok = await vscode.window.showWarningMessage(
    'Program the UART bootloader at 0x0000 over SWD? This needs the J-Link connected, and erases whatever is on the board. Afterwards, sketches upload over the USB adapter alone.',
    { modal: true }, 'Install');
  if (ok !== 'Install') return;
  const py = cfg().get('pythonPath') || 'python3';
  const tool = path.join(sdk, 'tools', 'ht5023_flash_seq.py');
  const cmd = `make bootloader && ${q(py)} ${q(tool)} ${q(path.join(sdk, 'build', 'boot.bin'))}` +
              ` --speed ${cfg().get('jlinkSpeed') || 1000}`;
  return runTask('install-bootloader', cmd, sdk, ['$gcc']);
}

async function doNewProject() {
  const sdk = findSdk();
  if (!sdk) return fail('No HT5023 SDK found — set ht5023.sdkPath so the project can point at the platform.');
  const picked = await vscode.window.showOpenDialog({
    canSelectFolders: true, canSelectFiles: false, canSelectMany: false,
    openLabel: 'Create the project here',
  });
  if (!picked || !picked.length) return;
  const dir = picked[0].fsPath;
  const platformDir = path.join(sdk, 'platformio');

  const ini = `[env:ht5023]
platform = ${platformDir}
board = ht5023
framework = arduino
monitor_speed = 115200

; Upload over SWD with a J-Link (default), or swap to the resident bootloader:
;   upload_protocol = bootloader
;   upload_port = /dev/ttyUSB0
upload_protocol = jlink

; Libraries from the PlatformIO registry, e.g.
; lib_deps =
;     bblanchon/ArduinoJson@^7.0.0
`;
  const main = `#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);          // UART2: PC11 = TX, PC12 = RX
  Serial.println("HT5023 up");
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
`;
  try {
    fs.mkdirSync(path.join(dir, 'src'), { recursive: true });
    fs.writeFileSync(path.join(dir, 'platformio.ini'), ini);
    const mainPath = path.join(dir, 'src', 'main.cpp');
    if (!exists(mainPath)) fs.writeFileSync(mainPath, main);
  } catch (e) {
    return fail(`Could not create the project: ${e.message}`);
  }
  await vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(dir), true);
}

function doDoctor() {
  const sdk = findSdk();
  const out = vscode.window.createOutputChannel('HT5023');
  const line = (s) => out.appendLine(s);
  const check = (label, cmd, note) => {
    const found = which(cmd);
    line(`  ${found ? '[ok]  ' : '[--]  '}${label.padEnd(22)}${found || 'not found' + (note ? ' — ' + note : '')}`);
    return !!found;
  };

  out.clear();
  out.show(true);
  line('HT5023 toolchain');
  line('');
  line(`  SDK              ${sdk || 'NOT FOUND — set ht5023.sdkPath'}`);
  line(`  Project kind     ${projectKind() || 'none detected'}`);
  line('');
  line('Required');
  check('arm-none-eabi-gcc', 'arm-none-eabi-gcc', 'sudo apt install gcc-arm-none-eabi');
  check('make', 'make', 'sudo apt install make');
  line('');
  line('Optional');
  check('JLinkExe (SWD)', 'JLinkExe', 'https://www.segger.com/downloads/jlink/');
  check('arduino-cli (.ino)', 'arduino-cli', 'run ./install-linux.sh');
  check('pio (PlatformIO)', 'pio', 'install the PlatformIO IDE extension');
  line('');
  const ports = listSerialPorts();
  line(`  Serial ports     ${ports.length ? ports.join(', ') : 'none detected'}`);
  line(`  Selected port    ${serialPort() || '(none)'}`);
  line(`  Example          ${cfg().get('example')}`);
  line('');
  if (sdk) line(`  Run ${path.join(sdk, 'install-linux.sh')} to install anything missing.`);
}

function doOpenFlasher() {
  const sdk = findSdk();
  if (!sdk) return fail('No HT5023 SDK found.');
  const py = cfg().get('pythonPath') || 'python3';
  const app = path.join(sdk, 'tools', 'ht5023_flasher.py');
  if (!exists(app)) return fail(`Flasher not found at ${app}`);
  const term = vscode.window.createTerminal({ name: 'HT5023 Flasher' });
  term.sendText(`${q(py)} ${q(app)}`);
  vscode.window.showInformationMessage('HT5023: the Flasher window is opening.');
}

// ------------------------------------------------------------- status bar --
function refreshStatus() {
  const kind = projectKind();
  if (!statusItems.length) return;
  const [target, build, upload, monitor] = statusItems;

  if (!kind) {
    statusItems.forEach((i) => i.hide());
    return;
  }
  target.text = kind === 'sdk'
    ? `$(circuit-board) HT5023: ${cfg().get('example')}`
    : `$(circuit-board) HT5023: ${kind === 'pio' ? 'PlatformIO' : 'sketch'}`;
  target.tooltip = kind === 'sdk' ? 'Pick which SDK example to build' : 'HT5023 project';
  target.command = kind === 'sdk' ? 'ht5023.selectExample' : 'ht5023.doctor';

  build.text = '$(tools) Build';
  build.tooltip = 'HT5023: Build';
  build.command = 'ht5023.build';

  upload.text = '$(arrow-up) Upload';
  upload.tooltip = 'HT5023: Upload over SWD (J-Link)';
  upload.command = 'ht5023.upload';

  const port = serialPort();
  monitor.text = `$(plug) ${port ? path.basename(port) : 'Monitor'}`;
  monitor.tooltip = port ? `HT5023: serial monitor on ${port}` : 'HT5023: serial monitor';
  monitor.command = 'ht5023.monitor';

  statusItems.forEach((i) => i.show());
}

// ---------------------------------------------------------------- lifecycle --
function activate(context) {
  const reg = (id, fn) =>
    context.subscriptions.push(vscode.commands.registerCommand(id, fn));

  reg('ht5023.build', doBuild);
  reg('ht5023.upload', () => doUpload(false));
  reg('ht5023.uploadUsb', () => doUpload(true));
  reg('ht5023.monitor', doMonitor);
  reg('ht5023.clean', doClean);
  reg('ht5023.selectExample', doSelectExample);
  reg('ht5023.selectPort', doSelectPort);
  reg('ht5023.installBootloader', doInstallBootloader);
  reg('ht5023.newProject', doNewProject);
  reg('ht5023.doctor', doDoctor);
  reg('ht5023.openFlasher', doOpenFlasher);

  for (let i = 0; i < 4; i++) {
    const item = vscode.window.createStatusBarItem(
      vscode.StatusBarAlignment.Left, 100 - i);
    statusItems.push(item);
    context.subscriptions.push(item);
  }
  refreshStatus();

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration('ht5023')) refreshStatus();
    }),
    vscode.window.onDidChangeActiveTextEditor(refreshStatus),
  );
}

function deactivate() {
  statusItems.forEach((i) => i.dispose());
  statusItems = [];
}

module.exports = { activate, deactivate };
