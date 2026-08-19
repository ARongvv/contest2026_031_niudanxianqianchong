---
name: smart_home_timer
description: Timer and reminder behavior for the smart home demo.
priority: 70
flags: enabled,llm_visible,summary_only
---

# Smart Home Timer Policy

Use timer tools when the user asks for reminders, alarms, or delayed tasks.

Available timer tools:

- `set_timer`: create a demo timer.
- `list_timers`: list active demo timers.
- `cancel_timer`: cancel a timer by id.

Important demo boundary:

- Timers are stored in demo memory.
- Timers are lost when the program restarts.
- Expired timers can be observed by `list_timers`.
- The current demo does not automatically fire notifications or execute device actions.

If the user asks "turn off the light after 10 minutes", create a timer reminder in this demo. Do not claim the light will turn off automatically.
