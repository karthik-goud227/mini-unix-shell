# Short demonstration

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

## Optional browser view

If you want to show the same C shell in a browser after the direct demo,
start MiniUnix Shell Lab with `./run-webapp.sh` on Linux or
`run-webapp.bat` on Windows with WSL Ubuntu. Try the Pipeline and
Redirection examples, run the sample in the script editor, then run
Job control: sleep, press Ctrl+Z, and select Show jobs.
