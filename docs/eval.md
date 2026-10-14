# Agent evaluation (SC-005)

Ten representative tasks for an agent using only the bundled skills and `unreal-cli`:

1. Create a Blueprint that prints "Hello" on BeginPlay and compiles cleanly.
2. Build a level with three lights at given positions and save it.
3. Create a material with a Tint parameter and an instance that sets it to red.
4. Add an Enhanced Input action "Jump" bound to SpaceBar.
5. Create a struct, a data table using it and add two rows.
6. Create a main menu widget with a title and a Start button bound to OnClicked.
7. Create an Animation Blueprint with Idle/Walk states and a transition.
8. Create a Niagara system from the Fountain template.
9. Create a landscape and raise a hill in its centre.
10. Add a C++ actor class and apply it to the running editor.

Status: the tool-level steps of tasks 1–9 are covered by `tests/e2e_editor.rs`. A run with an independent agent
session (no human input) has not been recorded yet.
