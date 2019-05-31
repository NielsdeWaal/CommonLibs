#TODO
-	Signal watcher -> When for example the application needs closing, we want to look for SIGINT
-	FD Watcher with callback to handler class -> OnFDRead and OnFDWrite
-	Timer structure -> Every cycle of the loop we need to check if a timer has expired {DONE}
-	Cycle stats -> See how many cycles have been ran every second {DONE}
-	Have option for choosing between normal timer and linux timerfd -> this would mean that users would have a choice between "run-hot" and waiting for timerfd
