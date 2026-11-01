/* Nonblocking dispatch ownership, adapted from portingpete/xboxrecomp
 * a40397f9baeceb6970fb3b484d93fe9d49925d91. Use the pinned POSIX HAL's
 * existing recursive lock; do not change the caller's current IRQL. */
int black_IrqlTryEnterDispatch(void)
{
    pthread_once(&g_dispatch_once, dispatch_lock_init);
    return pthread_mutex_trylock(&g_dispatch_lock) == 0;
}

void black_IrqlLeaveDispatch(void)
{
    dispatch_unlock();
}
