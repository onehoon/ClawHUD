# EC Diagnostics

The current Diagnostics implementation is intentionally limited to an MSI Claw EC read probe. Opening Settings or selecting the Diagnostics tab does not initialize WMI. Press **Start EC Test** to start one bounded worker.

The test connects to `ROOT\WMI`, invokes only `Get_Temperature`, `Get_Fan`, and `Get_Data`, records ten samples at approximately one-second intervals, then releases WMI. Each sample keeps the raw payload and records decoded CPU/GPU temperature, fan RPM, and CPU package power where the documented payload is available. The battery current and voltage selectors are preserved as raw bytes; system-power decoding remains deferred until hardware validation closes the documented sign/scaling questions.

Logs are UTF-8 text files under `logs/diagnostics/ec-YYYYMMDD-HHMMSS.txt`. **Open Log Folder** opens that directory. The log includes elevation state and records `Unavailable` for environment fields that cannot be obtained. A read failure for one selector does not stop the remaining selectors; WMI connection failure ends the test and is recorded.

No `Set_*`, TDP, fan-control, charge-limit, or ownership operation is implemented. The test is not production telemetry and has not been validated on physical MSI Claw hardware in this development environment.
