# Migration policy

Existing plug-ins vendor a pinned INHA Core snapshot under `Dependencies/INHA-Core`.
This keeps private GitHub Actions builds reproducible without requiring a cross-repository
credential. The snapshot records its upstream release in `.inha-core-version`.

To update a normal local Git checkout:

```powershell
git subtree pull --prefix Dependencies/INHA-Core `
  https://github.com/o-inha/VST3-INHA-Core.git release/v0.1.2 --squash
```

After an update, every affected plug-in must run its engine/default-state tests and a
Windows VST3 build. A plug-in never follows Core `main` implicitly.

Parameter IDs, component/controller CIDs and saved-state schemas remain owned by each
plug-in. A Core migration must not renumber or reinterpret them.
