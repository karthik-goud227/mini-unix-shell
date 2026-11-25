# MiniUnix Shell Lab

This is a local browser interface for the C shell. It starts `msh` inside a
Linux pseudo-terminal, which preserves terminal input and job-control
behavior. The interface adds examples, a script editor, and buttons for
Ctrl+C and Ctrl+Z. It does not replace the shell implementation.

## Run on Ubuntu or another Linux distribution

Install GCC, Make, and Python 3. Then, from the repository root:

```sh
./run-webapp.sh
```

Open <http://127.0.0.1:8765> and leave that terminal window open. Press
Ctrl+C in the server terminal to stop the app. No Python packages, Node.js,
or internet connection are required at runtime.

## Run on Windows with WSL Ubuntu

Install WSL and an Ubuntu distribution. In Ubuntu, install the build tools
and Python 3 once:

```sh
sudo apt update
sudo apt install build-essential python3
```

Clone or download this repository and extract it. Double-click
`run-webapp.bat` in the project folder. It builds `msh` in WSL and opens a
browser when ready. Keep the launcher window open while using the app;
press Ctrl+C in that window to stop it.

Each teammate can run their own copy on their PC. The app serves only that
computer's browser, so no account or shared server is needed. The project
folder can be copied to another PC after the prerequisites are installed.

If browser opening is blocked, manually visit <http://127.0.0.1:8765>.
The app binds only to the local computer. If port 8765 is occupied, run
`python3 webapp/server.py --port 8766` inside WSL and open that port.

## Behavior and limits

- The live terminal is a real `msh` session. Typed commands, quick examples,
  and the control-key buttons all send input to the same session.
- The script editor runs text in a separate script-mode `msh` process. Changes
  made inside that script do not modify the interactive parent shell.
- Reloading the browser keeps the shell session while the server is running.
  The editor draft is saved in that browser's local storage.
- The server is deliberately local only. It is intended for a project demo,
  not for hosting on a public network.
- xterm.js 6.0.0 and the Fit addon 0.11.0 are bundled under
  `webapp/static/vendor` with their MIT licenses, so teammates need no
  frontend installation.

The command-line shell remains available through `make msh` and `./msh`.
