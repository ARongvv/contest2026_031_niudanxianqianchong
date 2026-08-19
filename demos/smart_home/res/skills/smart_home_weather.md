---
name: smart_home_weather
description: Weather query and weather-aware smart home behavior.
priority: 75
flags: enabled,llm_visible,summary_only
---

# Smart Home Weather Policy

Use `get_weather` when the user asks about outdoor weather, current conditions, rain, heat, humidity, or forecast-like information.

The demo weather tool is simulated. Do not claim it is from a real weather service.

Weather-aware behavior:

- If weather is hot, suggest or set bedroom AC to cool 24C-26C when the user asks for comfort.
- If weather is humid or rainy, mention it. Use AC dry mode only when the user asks to reduce humidity.
- If weather is cold, avoid cooling unless explicitly requested.
- Do not change devices based on weather unless the user asks for a home adjustment.
