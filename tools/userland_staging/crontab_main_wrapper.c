/* Same pattern as cron_main_wrapper.c - real crontab.c's main() renamed
 * via -Dmain=crontab_cmd_real_main. */
extern int crontab_cmd_real_main(int argc, char **argv);

int
main(int argc, char **argv)
{
	return crontab_cmd_real_main(argc, argv);
}
