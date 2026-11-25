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
