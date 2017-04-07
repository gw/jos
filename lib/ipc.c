// User-level IPC library routines

#include <inc/lib.h>

// Receive a value via IPC and return it.
// If 'pg' is nonnull, then any page sent by the sender will be mapped at
//	that address.
// If 'from_env_store' is nonnull, then store the IPC sender's envid in
//	*from_env_store.
// If 'perm_store' is nonnull, then store the IPC sender's page permission
//	in *perm_store (this is nonzero iff a page was successfully
//	transferred to 'pg').
// If the system call fails, then store 0 in *fromenv and *perm (if
//	they're nonnull) and return the error.
// Otherwise, return the value sent by the sender
int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	// If `pg` is null, we use an address >UTOP
	// to signal that the receiver is not expecting
	// a page mapping. We can't use 0 b/c that's a
	// perfectly valid place to map a page. We can't
	// use -1 because that's not a valid pointer value.
	void *pg_arg = pg ? pg : (void *)(UTOP + 1);
	int r;
	if (r = sys_ipc_recv(pg_arg)) {
		*from_env_store = 0;
		*perm_store = 0;
		return r;
	}

	if (from_env_store)
		*from_env_store = thisenv->env_ipc_from;

	if (perm_store && thisenv->env_ipc_perm)
		*perm_store = thisenv->env_ipc_perm;

	return thisenv->env_ipc_value;
}

// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'toenv'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	// If `pg` is null, we use an address >UTOP
	// to signal that the receiver is not expecting
	// a page mapping. We can't use 0 b/c that's a
	// perfectly valid place to map a page. We can't
	// use -1 because that's not a valid pointer value.
	void *pg_arg = pg ? pg : (void *)(UTOP + 1);
	int perm_arg = pg ? perm : 0;
	int r;

	while (r = sys_ipc_try_send(to_env, val, pg_arg, perm_arg)) {
		if (r != -E_IPC_NOT_RECV)
			panic("sys_ipc_try_send failed with: %d", r);

		sys_yield();
	}
}

// Find the first environment of the given type.  We'll use this to
// find special environments.
// Returns 0 if no such environment exists.
envid_t
ipc_find_env(enum EnvType type)
{
	int i;
	for (i = 0; i < NENV; i++)
		if (envs[i].env_type == type)
			return envs[i].env_id;
	return 0;
}
