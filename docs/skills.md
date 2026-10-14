# Skills

Bundled skills live in `skills/<name>/SKILL.md` and are embedded in the binary.

```bash
unrealengine-cli skills list
unrealengine-cli skills install claude-code --local   # .claude/skills
unrealengine-cli skills install codex --local         # .agents/skills
unrealengine-cli skills install cursor --global       # ~/.cursor/skills
unrealengine-cli skills refresh --local               # after updating the CLI
unrealengine-cli skills uninstall cursor --local
unrealengine-cli skills lint --severity error
```

## Skill Contract v1

* Frontmatter: `name` (= directory), `description` (starts with "Use when", at most 300 characters), `version`,
  optional `tools: [...]` (each must exist in the tool catalog).
* Sections: `## When to use`, `## Steps`, `## Verify`, `## On failure` (none empty).
* `unrealengine-cli <command>` references must be real commands; relative links must exist; no machine-specific
  absolute paths; at most 500 lines.
