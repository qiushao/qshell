# QShell MCP Control Protocol

QShell can expose a local Streamable HTTP MCP endpoint for controlled terminal automation. The endpoint is disabled by default and, when enabled, listens only on:

```text
http://127.0.0.1:<port>/mcp
```

The implementation follows the MCP 2025-06-18 JSON-RPC lifecycle for `initialize`, `notifications/initialized`, `tools/list`, and `tools/call`. It returns HTTP 405 for GET because QShell does not provide an SSE stream.

## Enable MCP

1. Open `File` -> `Setting`.
2. Enable `Enable MCP`.
3. Keep the default port `8765` or choose another local port.
4. Copy the generated `MCP Token`, or click `Regenerate` to create a new token.
5. Click `OK`.

On Linux and macOS, clicking `OK` while MCP is enabled also:

- Writes `QSHELL_MCP_TOKEN` to `~/.config/qshell/mcp.env` with permissions `0600`.
- Detects the current shell from `$SHELL`.
- Adds `source "$HOME/.config/qshell/mcp.env"` to the matching startup file only when an MCP environment source line is not already present.

The supported startup files are `~/.bashrc` (Bash), `~/.zshrc` (Zsh), `~/.config/fish/config.fish` (Fish), `~/.kshrc` (Ksh/Mksh), and `~/.profile` (Sh/Dash/Ash). Open a new terminal after enabling MCP so it loads the new environment variable. Regenerating the token and clicking `OK` updates `mcp.env` without adding another source line.

Changing MCP settings restarts the local endpoint. Disabling MCP releases the listening port but leaves the environment file and startup-file source line in place for the next enable.

## Configure Codex

Codex CLI, the Codex IDE extension, and the ChatGPT desktop app share MCP settings on the same host. The global configuration file is `~/.codex/config.toml`; a trusted repository may instead use `.codex/config.toml` for project-scoped settings.

QShell must be running with MCP enabled before Codex starts. The token environment variable must be visible to the process that launches Codex. On Linux and macOS, start Codex from a new terminal after QShell completes the automatic shell setup.

### CLI setup

On Linux or macOS, QShell has already configured the token environment variable. From a new terminal, run:

```bash
codex mcp add qshell --url http://127.0.0.1:8765/mcp --bearer-token-env-var QSHELL_MCP_TOKEN
codex mcp get qshell
codex mcp list
```

For a one-off test in a supported POSIX shell, load the generated environment file manually before starting Codex:

```bash
source ~/.config/qshell/mcp.env
```

If QShell reports that the detected shell is unsupported, `mcp.env` is still created, but you must translate its assignment to that shell's environment-variable syntax manually.

In PowerShell:

```powershell
$env:QSHELL_MCP_TOKEN = '<token from QShell settings>'
codex mcp add qshell --url http://127.0.0.1:8765/mcp --bearer-token-env-var QSHELL_MCP_TOKEN
codex mcp get qshell
codex mcp list
```

If a `qshell` entry already exists, remove it with `codex mcp remove qshell` before adding it again. Restart Codex or its IDE extension after changing the configuration or token. In the Codex terminal UI, run `/mcp` to confirm that `qshell` connected and that its tools are available.

PowerShell `$env:` affects only the current process and its children. On every platform, Codex must be restarted after changing the token. Do not commit the token or `mcp.env` to this repository.

### config.toml setup

The equivalent manual configuration is:

```toml
[mcp_servers.qshell]
url = "http://127.0.0.1:8765/mcp"
bearer_token_env_var = "QSHELL_MCP_TOKEN"
enabled = true
default_tools_approval_mode = "writes"
```

`default_tools_approval_mode = "writes"` asks before tools that can change terminal state while allowing status and screen-reading tools without an extra prompt. The wait tools default to 30 seconds. If you use their maximum 300-second timeout, also set `tool_timeout_sec = 310` because Codex otherwise applies its own shorter tool timeout.

See the [official Codex MCP documentation](https://developers.openai.com/codex/mcp) and [configuration reference](https://developers.openai.com/codex/config-reference) for the current list of supported options.

### End-to-end check

After restarting Codex, ask it:

```text
Call qshell_get_status and return its structured result.
```

A successful result contains the QShell version plus `mcp.listening: true`. `codex mcp get` and `codex mcp list` validate saved configuration only; `/mcp` or a real tool call validates the live connection and bearer token.

### Troubleshooting

- `401 Unauthorized`: `QSHELL_MCP_TOKEN` is missing from the Codex process or does not match the token in QShell settings.
- Connection refused: QShell is not running, MCP is disabled, or the configured port differs from the QShell setting.
- Server is configured but tools are absent: restart the Codex client so it initializes the server and refreshes the tool list.
- Tool call times out near 60 seconds: increase `tool_timeout_sec` when deliberately using a longer wait tool timeout.
- A regenerated QShell token invalidates the old environment value; update it and restart Codex.

## Security

The MCP endpoint is intended for local automation only.

- It binds only to `127.0.0.1`.
- Every request must include `Authorization: Bearer <token>`.
- Requests with an `Origin` header are accepted only from `localhost`, `127.0.0.1`, or `[::1]`.
- On Unix-like systems, QShell restricts its configuration file, which contains the MCP token, to the current user (`0600`).
- The generated `~/.config/qshell/mcp.env` is also restricted to the current user (`0600`). Shell startup files contain only a source command, not the token itself.
- Session listing omits passwords, SSH private-key passphrases, and other secret fields.
- The server does not expose arbitrary Lua execution.

## Tools

| Tool | Purpose |
| --- | --- |
| `qshell_get_status` | Return QShell version, tab count, current tab name, and MCP listener state. |
| `qshell_list_sessions` | Return configured session `id`, `name`, `protocol`, and `groupId`. |
| `qshell_open_session_by_id` | Open a configured session by `sessionId`. |
| `qshell_open_session_by_name` | Open a configured session by `sessionName`. |
| `qshell_switch_tab` | Switch to a tab by zero-based `index` or tab `name`. |
| `qshell_next_tab` | Switch to the next tab. |
| `qshell_connect_current` | Connect the current tab if disconnected. |
| `qshell_disconnect_current` | Disconnect the current tab if connected. |
| `qshell_send_text` | Send `text` to the current terminal. Supports `interpretEscapes` for `\r`, `\n`, and `\t`. |
| `qshell_send_key` | Send a named key such as `Enter`, `Tab`, `Ctrl+C`, `Up`, or `F1`. |
| `qshell_get_screen_text` | Return visible text from the current terminal screen. |
| `qshell_get_last_line` | Return the last visible terminal line. |
| `qshell_clear_screen` | Clear the current terminal screen. |
| `qshell_zmodem_upload` | Set `filePaths` for the next remote `rz` transfer without opening a file chooser. |
| `qshell_zmodem_download` | Set a writable local `directoryPath` for the next remote `sz` transfer without opening a directory chooser. |
| `qshell_wait_for_string` | Wait for `text` in terminal output until `timeoutMs` or `timeoutSeconds`. |
| `qshell_wait_for_regex` | Wait for `pattern` in terminal output until `timeoutMs` or `timeoutSeconds`. |

All tool results include MCP `content` text and `structuredContent` JSON. Operational failures, such as no current terminal or a timeout, are returned as tool results. JSON-RPC protocol errors, such as unknown methods or malformed requests, are returned as JSON-RPC errors.

The ZMODEM tools prepare one transfer on the current connected terminal. Call
the preparation tool before using `qshell_send_text` to run `rz` or `sz`.
The download directory must already exist and be writable. A prepared path is
consumed by the next matching ZMODEM handshake; transfers started without a
matching preparation keep the normal interactive chooser behavior.

For example, prepare a download and then start `sz` on the remote terminal:

```json
{"name":"qshell_zmodem_download","arguments":{"directoryPath":"/tmp/downloads"}}
{"name":"qshell_send_text","arguments":{"text":"sz /var/log/app.log\\r"}}
```

Prepare one or more uploads before starting remote `rz`:

```json
{"name":"qshell_zmodem_upload","arguments":{"filePaths":["/tmp/report.txt","/tmp/result.bin"]}}
{"name":"qshell_send_text","arguments":{"text":"rz\\r"}}
```

## Manual Protocol Check

Replace `$QSHELL_MCP_TOKEN` and the port as needed:

```bash
curl -sS http://127.0.0.1:8765/mcp \
  -H "Authorization: Bearer $QSHELL_MCP_TOKEN" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"curl","version":"1"}}}'
```

Send the initialized notification (HTTP `202` is expected):

```bash
curl -sS -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8765/mcp \
  -H "Authorization: Bearer $QSHELL_MCP_TOKEN" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "MCP-Protocol-Version: 2025-06-18" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
```

Then list tools:

```bash
curl -sS http://127.0.0.1:8765/mcp \
  -H "Authorization: Bearer $QSHELL_MCP_TOKEN" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "MCP-Protocol-Version: 2025-06-18" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
```

Finally, call a read-only tool:

```bash
curl -sS http://127.0.0.1:8765/mcp \
  -H "Authorization: Bearer $QSHELL_MCP_TOKEN" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "MCP-Protocol-Version: 2025-06-18" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"qshell_get_status","arguments":{}}}'
```
