# Theta project memory

## Working preferences

- Build a polished native desktop DAW. Prioritize Windows while preserving macOS portability.
- Treat UI frame pacing and immediate pointer response as core requirements.
- Preserve existing work, including the original Electron prototype. Avoid large mock projects or benchmark scaffolding unless requested.
- The product is Theta and the repository is https://github.com/monkjuice/THETA.git. The local workspace may still be named `theda`; retain legacy project compatibility deliberately.

## Commit and push workflow

- The user explicitly wants regular pushes and good Git practices. At meaningful completed milestones, make focused commits and push to the current branch's upstream. Routine commits and pushes are authorized without repeated confirmation.
- Before committing, inspect status and the diff, run relevant existing checks, and ensure the commit contains only intended changes. Preserve unrelated user edits and never include secrets, build output, caches, or downloaded dependencies.
- Use clear commit messages describing the result. Avoid accumulating a large amount of completed work locally; push verified milestones before handing them back to the user.
- Confirm the destination remote and branch before pushing. If the remote has moved, inspect and reconcile safely; never force-push or rewrite shared history without explicit authorization.
- Match validation to the change: native behavior changes need relevant native checks; documentation-only edits need a diff review, not a full build. Do not add tests that merely mirror low-impact changes.
- Report what was committed and pushed, the relevant validation, and any blockers. Never describe a local commit as pushed until the push succeeds.
