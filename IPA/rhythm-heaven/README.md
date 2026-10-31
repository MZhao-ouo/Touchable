# Rhythm Heaven IPA

This folder holds the current Rhythm Heaven package and its checksum records.
The current package is build 47.
It supports iPhone, iPad, a separate in-game language setting, controller haptics, and a live border glow.
The touch buttons use Liquid Glass.
Build 29 fixes BeatSpell performance and touch.
Build 30 rewrites the border glow to make the light more natural. Build 31 makes the letterbox border follow the picture faster. Build 32 makes the transition between the picture edge and the letterbox border smoother. Build 33 declares Game Mode and stays at 60 Hz. In build 34, a pressed glass button shows only the system effect. Build 35 hides the touch controls on loading screens. Build 36 makes every visible row of a long list tappable and adds an optional TV mode. Build 37 switches TV mode without a restart. Build 38 chooses TV or handheld mode from the audio output and has no mode button. Build 39 adds touch for the Rhythm Toys (Who's Got Rhythm? and the sound boards), a full controller for Drum Lessons, and localized app names. Build 40 makes the level map select-then-confirm: a tap on the selected level starts it. Build 41 lets two players share the touch screen in multiplayer and gives each real controller its own player. Build 42 sets up the multiplayer members by touch. Build 43 gives the two-player halves the single-player touch glow and A only in "A or ◀" levels. Build 45 adds the optional voice play setting (build 44 had it without the delay reminder and was never installed). Build 46 tries to ignore the game's own sound in voice play and adds a sensitivity setting. Build 47 uses Apple's echo cancellation for voice play instead.
The current IPA is `RhythmHeaven-build47-resigned-20261027-011557.ipa`: build 47 re-signed on 2026-10-27 with a profile for the iPhone and the iPad mini (expires 2026-11-03). The app also has the later weak `ss_audio_set_input_handler()` default in App.m.
Its SHA-256 is `13d039db05a57fa79523e936803c875eb1a7287d7a65428fe79c6c48761a1866`.
The iPhone-only re-signed IPA (`...-010734`) moved to `.touchable/archive/ipa/rhythm-heaven/resign-20261027-011557/`.
The first build 47 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/resign-20261027-010734/`.
The build 46 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build47/`.
The build 45 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build46/`.
The build 43 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build44/`; build 44 is in `before-build45/`.
The build 42 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build43/`.
The build 41 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build42/`.
The build 40 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build41/`.
The build 39 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build40/`.
The re-signed build 47 is installed on the iPhone (2026-10-27, `-010734` signature, save backup `phone-before-install-20261027-011308-41d9f1`) and on the iPad mini (2026-10-27, `-011557` signature, save backup `phone-before-install-20261027-012623-1f1160`). The launcher backed up the saves before each install.
The signing profile expires on 2026-11-03 and lists the iPhone and the iPad mini.
The build 38 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build39/`.
The build 37 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build38/`.
The build 36 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build37/`.
The build 35 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build36/`.
The build 34 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build35/`.
Controller haptics are verified only on the iPad mini with a DualSense.
The build 33 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build34/`.
The build 32 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build33/`.
The build 31 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build32/`.
The build 30 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build31/`.
The build 29 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build30/`.
The build 28 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build29/`.
The build 27 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build28/`.
The build 26 IPA, JSON, and SHA-256 moved to `.touchable/archive/ipa/rhythm-heaven/before-build27/`.
The build 19 and build 20 IPA, JSON, and SHA-256 stay in `.touchable/archive/ipa/rhythm-heaven/before-build26/`.
The language setting is at "Settings > Apps > Rhythm Heaven > Language".
Close the game on the device. Then install with "Install existing IPA" in the top-level launcher.
The launcher build actions currently refuse Rhythm Heaven. For the reason, see the known limits in `internal/games/rhythm-heaven/README.md`.
