/* The mailbox service starts through INIT_APP_EXPORT. CPU1 must not mount,
 * resize or format the Linux SD card, or run CanMV's USB/network startup.
 */
int main(void)
{
    return 0;
}
