# TeXstudio with TeXpresso live preview

This fork adds an automatic native Qt preview using the same pinned TeXpresso
engine as `DominikPeters/texpresso-vscode`. The engine is bundled with the
application; install TeX Live for its packages. Each open project retains its
own session and preview position when switching tabs. Normal TeXstudio build
commands remain available for final PDFs.

Open a saved `.tex` project to start. The preview includes page navigation,
zoom, Pause/Resume, Stop, Restart, Output, and Sync controls. Command-click on
macOS (Ctrl-click on Linux) jumps from the preview to the source.
View → Show → Live Preview enables or disables automatic preview.

See [native build instructions](native/README.md) and the
[integration design and implementation status](docs/texpresso-integration.md).


[CI]: https://github.com/texstudio-org/texstudio/workflows/CI/badge.svg
[Codacy Badge]: https://app.codacy.com/project/badge/Grade/892de515e8c54a7a99bd836743c4510c
[Codacy TeXstudio]: https://app.codacy.com/gh/texstudio-org/texstudio/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade

[Website]: https://www.texstudio.org

[Download]: https://www.texstudio.org/#download
[Releases]: https://github.com/texstudio-org/texstudio/releases/
[News]: https://www.texstudio.org/#news
[Contribute]: https://github.com/texstudio-org/texstudio/wiki/Contribute
[User Manual]: https://texstudio-org.github.io/getting_started.html
[Dev Builds]: https://github.com/texstudio-org/texstudio/actions/workflows/cd.yml


# TeXstudio
![CI] [![Codacy Badge]][Codacy TeXstudio]

**TeXstudio** is a fully featured **LaTeX** editor.

Our goal is to make ***writing LaTeX documents as easy and comfortable*** as possible.

---
#### 〔 [Download] / [Releases] / [Dev Builds] 〕〔 [News] 〕〔 [User Manual] 〕〔 [Contribute] 〕
---


### Features
- Advanced **Syntax Highlighting**
- Live **Reference Checking**
- An integrated **PDF viewer**
- Live **Inline Preview**
- **Latex Commands completion and syntax checking**
- Live **Grammar checking**
- Local and global **structure view**
- **Citations checking and completion**
- **Spellchecking**

<br>

#### Find out more at our [Website].
