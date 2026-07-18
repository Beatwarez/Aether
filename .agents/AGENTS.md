# Antigravity Workflow Rules

1. **Implementation Plan Workflow**:
   - Whenever the user presents a problem, the agent must first research, formulate a detailed implementation plan (`implementation_plan.md`), and present it to the user.
   - The agent must **wait** for the user's explicit command or approval to proceed (e.g. "proceed", "go ahead").
   - Once the command/approval to proceed is given, the agent must start execution immediately.
2. **Implicit Permission / Auto-execution**:
   - Once the user gives the proceed order, the agent should perform all edits, builds, git pushes, and verifications **completely autonomously**.
   - Do not stop to ask for confirmation or intermediate permissions during implementation. Assume the user approves all actions, commands, and file writes.
