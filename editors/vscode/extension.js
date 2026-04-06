const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

function activate(context) {
    const config = vscode.workspace.getConfiguration('hexa-lang');
    const serverPath = config.get('server.path', 'hexa');
    const serverArgs = config.get('server.args', ['--lsp']);

    const serverOptions = {
        run: { command: serverPath, args: serverArgs, transport: TransportKind.stdio },
        debug: { command: serverPath, args: serverArgs, transport: TransportKind.stdio }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'hexa' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.hexa')
        }
    };

    client = new LanguageClient('hexa-lang', 'HEXA Language Server', serverOptions, clientOptions);
    client.start();
}

function deactivate() {
    if (client) return client.stop();
}

module.exports = { activate, deactivate };
