---
title: Cool, but What Can I do with It?
---

## Cool, but What Can I Do with It?
---

AppScope offers APM-like, black-box instrumentation of any unmodified Linux executable and application. You can use AppScope in single-user troubleshooting, or in a distributed production deployment, with little extra tooling infrastructure. Especially when paired with Cribl LogStream, AppScope can deliver just the data you need to your existing tools.

### Instrument, collect and observe

- Metrics about process and application performance.
- Logs emitted from an application - with zero configuration - delivered to log files or to the console.
- Network flow logs, metrics and all DNS requests.
- File open and close operations, with I/O consumption per file.
- HTTP requests to and from an application, including URI endpoint, HTTP header, and full payload visibility.
- HTTPS too :)

AppScope works with static or dynamic binaries and can instrument anything running in Linux. The CLI makes it easy to inspect any application without needing a man-in-the-middle proxy. If you're feeling adventurous, you can use the AppScope library independently of the CLI, with even more fine-grained configuration options.

AppScope collects and forwards StatsD-style metrics about running applications and with HTTP-level visibility, any web server or application can be instantly observable. Its output allow you to use general-purpose tools instead of specialized APM tools and agents.

### Basic example use cases:

- Send HTTP events from Slack to a specified Splunk server.
- Send metrics from nginx to a specified Datadog server.
- Send metrics from a Go static application to a specified Datadog server.
- Run Firefox from the AppScope CLI, and view results on a terminal-based dashboard.
- Run Google Chrome from the AppScope CLI, and view results on a terminal-based dashboard. And be surprised.