# Antigravity Workflow Rules

1. **Implementation Plan Workflow**:
   - Whenever the user presents a problem, the agent must always answer first, then ask if the user needs an implementation plan.
   - If a plan is requested (e.g. if the user says "plan"), formulate a detailed implementation plan (`implementation_plan.md`) and present it to the user.
   - The agent must **wait** for the user's explicit command or approval to proceed (e.g., "proceed", "go ahead").
   - Just after the command/approval to proceed is given, the agent must start editing the code.

2. **Code Editing & Parity**:
   - When editing a file, always check for any discrepancies in other files to ensure they are always in parity (e.g., header vs cpp files, JS vs C++ parameter names, etc).

3. **Implicit Permission / Auto-execution**:
   - Once the user gives the proceed order, the agent should perform edits, builds, and verifications **completely autonomously**.
   - Do not stop to ask for confirmation or intermediate permissions during implementation. Assume the user approves all actions, commands, and file writes.
   - **Never** push changes to GitHub unless explicitly asked by the user.

4. **JUCE Audio Plugin Development Rules**:
   - Refer to `plugin_rules.md` in `JUCE_projects` for mandatory state serialization, typed parameter getters (`.get()`), `isInitializing` locks, and CSS-native Web View scaling rules.
