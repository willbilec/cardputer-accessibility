# Earlier firmware experiments

Launcher, CardEX and Cardulator were developed in separate upstream checkouts. The patches and accompanying additions preserve the local adaptations. They are historical experiments. Use the unified firmware target for the public release.

From the repository root, run `python fetch_dependencies.py Launcher CardEX cardulator` to obtain the base revisions. To reconstruct one adaptation, apply its patch from the root with `git -C research/NAME apply ../patches/NAME.patch`, then copy the contents of `research/patches/NAME-additions/` into `research/NAME/`, preserving paths. See the guides inside each checkout and the [project status](../../PROJECT_STATUS.md) for their original build and test context.

BackgroundAudio and its eSpeak NG sources ship under GPL terms. Their corresponding source is obtained at the revision in [dependencies.json](../dependencies.json), including the pinned eSpeak submodule. The build applies the changes in [prepare_background.py](../offline-reader/prepare_background.py). The HID adaptation uses [prepare_hid.py](../offline-reader/prepare_hid.py) and retains its [MIT notice](../offline-reader/licenses/HID-MIT.txt). Other libraries and experimental ports retain the terms and notices in their pinned upstream repositories.
