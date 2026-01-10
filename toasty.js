// Toasty Notification Plugin for OpenCode
//
// Installation:
// 1. Run: toasty --install opencode
//    OR manually:
//    - Download toasty-x64.exe from https://github.com/shanselman/toasty/releases
//    - Place toasty-x64.exe in your PATH or a known location
//    - Place this file (toasty.js) in ~/.config/opencode/plugin/
//    - Update the toastyPath below to point to your toasty.exe location
// 2. Restart OpenCode
//
// Shows a Windows toast notification when a session completes.

export const ToastyPlugin = async ({ $ }) => {
  const toastyPath = `${process.env.USERPROFILE}/.config/opencode/plugin/toasty-x64.exe`

  return {
    event: async ({ event }) => {
      if (event.type === "session.idle") {
        await $`${toastyPath} "OpenCode finished" -t "OpenCode"`
      }
    },
  }
}
