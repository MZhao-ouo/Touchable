# Coding agent instructions

Use ASD-STE100 writing style. Write short sentences. Use one action per instruction. Keep code identifiers unchanged.

Touchable adapts user-provided Switch games for an Apple Silicon Mac host and an iPhone app. Each game has its own configuration and adapter. The games share runtime services and the iOS app layer.

## Start a task

1. Read [the project summary](docs/developer/project-summary.md).
2. Select the applicable procedure in [the agent workflow](docs/developer/agent-workflow.md).
3. Read `internal/games/<id>/game.json`, `profile.json`, and `README.md` for the selected game.
4. Check the existing files before you change them.
5. Set the expected result and the required checks.

Use `.touchable/venv/bin/python` for project tools. Pass `--game <id>` to each tool that accepts it. Most tools select `silksong` when you omit this option.

Use `internal/tools/workspace.py` for game paths in Python tools. Use `game.json` for application identity, source lists, adapter selection, and active content paths. Use `profile.json` for module hashes, Build IDs, tool revisions, and shader options.

## Put changes in the correct location

| Change | Location |
| --- | --- |
| Fixed game addresses or engine behavior | `internal/games/<id>/runtime/` |
| Shared Switch services | `internal/runtime/common/` |
| Shared iPhone display, input, audio session, or haptics | `internal/platform/ios/` |
| Game identity, source lists, or build options | `internal/games/<id>/game.json` |
| Module identity or shader layout | `internal/games/<id>/profile.json` |
| Extraction, analysis, generation, or packaging tools | `internal/tools/` |
| Changes to the pinned upstream code | `.touchable/dependencies/d2r-mobile/` and `internal/patches/d2r-mobile.patch` |

Each game implements `touchable_game_install()` and `touchable_game_host_call()`. Their interface is in `internal/runtime/common/touchable_game.h`.

## Preserve project constraints

- Keep each game's Bundle ID, product name, and save namespace separate.
- Preserve the existing phone save path unless the task includes a save migration.
- Keep `module_names` equal to `profile.runtime_modules`. Do not execute the Switch `rtld` module.
- Check module hashes and Build IDs before you use fixed addresses.
- Keep new games at `not-adapted` until their adapters permit development builds. Use `in-progress` during adaptation. Use `adapted` only with recorded game tests.
- Keep game files and keys in `MyGameFiles/`. Keep extracted content in `.touchable/work/<id>/`. Keep local saves and phone backups in `.touchable/saves/<id>/`.
- Use a new output path for extraction or CPU rewrite trials. Preserve the active modules and prior reports.
- Use a separate save directory for host tests. Use a copy when an existing save is required.
- Change the generators instead of editing generated Xcode projects, module tables, or `touchable_game_config.h`.
- Generate `internal/games/<id>/runtime/ss_shader_chain.h` with `internal/tools/build_shaders.py`.
- Preserve upstream edits. Record task changes in the applicable patch against the pinned upstream revision.
- Keep original packages, keys, saves, signed apps, and existing IPA files out of cleanup operations.

## Search and review

Search the source directories first. Exclude generated files and tool dependencies:

```sh
rg -n 'SEARCH_TEXT' internal/games internal/runtime internal/platform internal/tools internal/tests docs internal/patches \
  -g '!**/vendor/**' -g '!**/dotnet/**' -g '!**/__pycache__/**'
```

The project root has a local Git repository as of 2026-10-16. Check its status at task start. Keep local game data, keys, saves, signing files, apps, and IPA files outside Git. If Git is absent in another copy, use file comparisons and checksums. The upstream source has its own Git repository. Its modified files can contain the existing project patch.

### Local and public history

The public repository is `origin` (`MZhao-ouo/Touchable` on GitHub). Its `main` has a different history from the local `main`. This is intentional. It is not divergence.

- The public history starts with the commit "Initial public release of Touchable". That commit replaces the private local history before the public release.
- Each later local commit has a public copy with the same change and a different commit ID.
- The two histories have no merge base. `git rev-list --count` then reports commits on both sides. Do not report this as divergence.
- To compare the two branches, compare their trees: `git rev-parse HEAD^{tree}` and `git rev-parse origin/main^{tree}`. Equal trees mean equal content.
- Do not pull, merge, or rebase between the two branches. Copy a change to the other branch with `git cherry-pick`.
- A pull request merges on the public `main`. Copy its merged commits to the local `main` with `git cherry-pick`. Keep the original author.

## Verify and finish

Select checks from the workflow's validation table. Run checks for the changed behavior. Check both games after a shared runtime or graphics change. For documentation changes, check links, file references, and command syntax.

The Python test command does not run the C tests or the game-specific scripts. A bounded host run does not prove gameplay success. Read its log and record the observed result.

Keep new evidence in `.touchable/work/<id>/reports/` and `.touchable/build/games/<id>/logs/`. State whether a result came from a unit test, a Mac host test, or an iPhone test. Record checks that could not run.

Update the relevant game record when behavior, version, or test coverage changes. Update the project summary when project boundaries or game status changes. Finish with the changed files, test results, remaining limits, and any required next action.
