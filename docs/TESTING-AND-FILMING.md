# Test Pip and film your first Git snack

## What keeping Pip alive means today

Pip cannot die. There is no health decay, hunger meter, illness, or evolution yet. The app is a living-animation and Git-reward prototype, not a complete Tamagotchi simulation. A day off never removes progress.

- Pet Pip with **OK**; it reacts and affection increases.
- Use **Down** to play, **Up** to sleep, **Left/Right** to walk and wake.
- Keep the app open for animation. Unplugging USB does not stop the on-device behavior.
- Make eligible Git progress for food. The companion needs to be running on your computer to deliver Git events.
- Press **Back** to save affection and lifetime meals before closing the app or powering off.

There is no cloud account, ongoing model cost, or required GitHub login for the companion. GitHub login is only needed for your normal Git push.

## Start the companion

Close qFlipper and other serial clients. Open Gitgotchi on the device. From the repository root, with your virtual environment active:

```sh
python companion/app.py --repo . --serial /dev/cu.usbmodemYOUR_FLIPPER
```

On startup, the companion samples the device tick counter and calibrates animation pacing; this keeps scene holds consistent even on firmware with a different tick rate.

Find your port using `python -m serial.tools.list_ports`. Add `--port 8766` if another preview occupies port 8765. Open the localhost URL the companion prints.

Feeding uses one simple cycle: **commit + push = one meal and one grid square**. Playing and petting give no food or grid credit. Hold **Right** on Flipper to open today’s grid; Back returns to Pip. The browser shows the same daily progress.

## Do one real end-to-end test

Check your upstream first:

```sh
git status
git branch -vv
```

Your branch should track the intended remote branch. The companion never chooses a push destination or pushes for you. Start the companion before making the test change so it can establish its baseline.

Make a small useful change, such as adding a demo note:

```sh
printf '\nTested Pip feeding after a successful Git push.\n' >> demo-notes.md
git add demo-notes.md
git commit -m "Document Pip's Git snack demo"
```

Within about two seconds the device should show **COMMIT DETECTED**. Wait a few seconds so the camera catches it, then run:

```sh
git push
```

Expected sequence:

1. **COMMIT DETECTED** — at least roughly six seconds of notice if commit and push arrive close together.
2. **PUSH CONFIRMED** — about two seconds, with a falling snack parcel.
3. **SNACK DELIVERY!** — Pip walks to the bowl and eats.
4. **THANK YOU!** — little happy hops and hearts, then ordinary life resumes.

The companion reports `USB confirmed: commit` and `USB confirmed: feed`. The feed acknowledgement increases the device's meal total before the eating scene finishes. The browser shows the pending push and daily reward count.

"Push confirmed" means the local branch's configured upstream tracking ref contains the pending commit. A normal successful `git push` updates that ref. A failed push should not feed Pip. An external fetch can also establish upstream presence; this is not a network receipt checker or CI status check.

The default daily goal is **three completed cycles per local day per repository**, adjustable with `--daily-goal 1` through `--daily-goal 12`. It is a minimum, not a cap: extra cycles still feed Pip. Reaching the goal triggers a special celebration. The grid resets when the running companion observes a new local date. Unpushed pending progress survives a companion restart. Old history, empty commits, and backward/divergent movement do not earn food. Multiple commits between polls may collapse into one reward.

## Film it with your phone

A real physical screen is the most convincing version of this demo. Use a desk light without glare, stabilize the phone, and keep Flipper in focus. Turn off screen auto-dimming in your computer's recording setup if needed; no Flipper firmware settings need changing.

Suggested 20–30 second shot:

| Shot | What happens |
| --- | --- |
| Opening | Flipper is in frame, Pip wanders; the terminal shows your project. |
| Commit | Type the commit command and hold on **COMMIT DETECTED**. |
| Push | Type `git push`; keep its successful output visible. |
| Payoff | Move attention to the Flipper for the parcel, eating, and thank-you scene. |

For a clean software capture, screen-record the terminal beside the **Actual Flipper screen · live USB** panel at the top of the companion. That panel streams real device frames; no second serial connection or qFlipper is needed. The interactive browser pet below it is a separate renderer/save. Label that lower pet as a preview if you use it in a video.

Do not show credentials, private file content, or unrelated terminal scrollback. Use the public Gitgotchi repo or a purpose-made demo repository.

## Quick interaction checks without Git

The screenshot utility can also send test interactions. Stop the companion first to release the serial port:

```sh
python tools/capture_flipper.py --serial /dev/cu.usbmodemYOUR_FLIPPER --action commit --output commit.png
python tools/capture_flipper.py --serial /dev/cu.usbmodemYOUR_FLIPPER --action feed --wait 2 --output delivery.png
```

These are explicit simulated CLI events, not proof that a commit or push happened. `feed` increases the physical pet's lifetime meal count and does not add grid credit.

## Regression checks

```sh
python -m unittest discover -s tests -v
```

Tests use a temporary local bare remote to verify that a commit alone does not feed in push mode, a successful real push does, restarting while waiting does not lose the pending reward, and the same push cannot feed twice.
