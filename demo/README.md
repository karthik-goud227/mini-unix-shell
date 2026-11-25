# Short demonstration

For the visual demo, start MiniUnix Shell Lab with `./run-webapp.sh` on
Linux or `run-webapp.bat` on Windows with WSL Ubuntu. In the browser, use
the Pipeline and Redirection examples, run the sample in the script editor,
then choose Job control, press Ctrl+Z, and choose Show jobs. This shows
the same C shell through a real terminal session.

The command-line demonstration below is also available without the browser.

Build from the repository root with `make msh`, then run
`./msh demo/final_demo.msh`. The expected lines are `loop=alpha`,
`loop=beta`, `sourced_variable=after`, and `1`.

For an interactive terminal demonstration, run `./msh`, then enter:

```text
sleep 30
```

Press Ctrl+Z. Run `jobs`, then `bg %N` and `fg %N`, replacing N with the
job ID printed by the shell. Press Ctrl+C to finish the foreground sleep.
Job control requires a real terminal.
