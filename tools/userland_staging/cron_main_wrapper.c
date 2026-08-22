/* Real cron.c's own main() is renamed via -Dmain=cron_cmd_real_main (same
 * pattern build_ifconfig.sh already established) - this is the actual
 * entry point the linker sees. Nothing to translate: cron.c's real
 * main(argc, argv) signature is already exactly what this project's
 * crt0/_start hands it. */
extern int cron_cmd_real_main(int argc, char **argv);

int
main(int argc, char **argv)
{
	return cron_cmd_real_main(argc, argv);
}
