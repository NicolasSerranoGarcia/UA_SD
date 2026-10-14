- hcp protocol must also give the ID provided in the application arguments of Monitor and give it to Engine
- the listener in engine should handle more than one backlog. Remember that connections that exceed that number will be
discarded. If the connection resets between engine and monitor, they will
need to connect again, meaning that the listener socket could potentially throw out our connection. There should be some kind
of authentication of the monitor so that the engine knows it is correct.
- the engine is missing the queue monitoring
- the engine is also missing proper health checks: timeouts and error handling
- the monitor is missing NAP parsing when HC fails
