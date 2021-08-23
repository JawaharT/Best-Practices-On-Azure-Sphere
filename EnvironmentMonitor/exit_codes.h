// Custom Exitcodes for main
typedef enum {
	ExitCode_Telemetry_Buffer_Too_Small = 1,
	ExitCode_Button_Timer_Poll = 2,
	ExitCode_Not_Read_Button_A_GPIO = 3,
	ExitCode_Not_Read_Button_B_GPIO = 4,
	ExitCode_PollFd_Problem = 5,
	ExitCode_ButtonTimerFd_Problem = 6,
	ExitCode_Azure_IoT_Central_Connection = 7
} exit_codes;