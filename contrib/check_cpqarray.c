/*
   check_cpqarray, an extension for Netsaint / Nagios to check the
   status of a Compaq SmartArray controller from the commandline.
   Copyright (C) 2003  Guenther Mair

   based on the work and using main parts of

   CpqArray Deamon, a program to monitor and remotely configure a 
   SmartArray controller.
   Copyright (C) 1999  Hugo Trippaers

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "/usr/src/linux/drivers/block/ida_ioctl.h"
#include "/usr/src/linux/drivers/block/ida_cmd.h"
#include "/usr/src/linux/drivers/block/cpqarray.h"


const char *controllers[] =
{
  "/dev/ida/c0d0",
  "/dev/ida/c1d0",
  "/dev/ida/c2d0",
  "/dev/ida/c3d0",
  "/dev/ida/c4d0",
  "/dev/ida/c5d0",
  "/dev/ida/c6d0",
  "/dev/ida/c7d0"
};

const char *statusstr[] = {
        "Logical drive /dev/ida/c%dd%d: OK\n",
        "Logical drive /dev/ida/c%dd%d: FAILED\n",
        "Logical drive /dev/ida/c%dd%d: not configured.\n",
        "Logical drive /dev/ida/c%dd%d: using interim recovery mode, %3.2f%% done.\n",
        "Logical drive /dev/ida/c%dd%d: ready for recovery operation.\n",
        "Logical drive /dev/ida/c%dd%d: is currently recovering, %3.2f%% done.\n",
        "Wrong physical drive was replaced.\n",
        "A physical drive is not properly connected.\n",
        "Hardware is overheating.\n",
        "Hardware has overheated.\n",
        "Logical drive /dev/ida/c%dd%d: currently expanding, %3.2f%% done.\n",
        "Logical drive /dev/ida/c%dd%d: not yet available.\n",
        "Logical drive /dev/ida/c%dd%d: queued for expansion.\n",
};

extern char *optarg;
extern int optind, opterr, optopt;

int ctrls_found_num;
int exit_code = 0;
struct controller ctrls_found[8];

#define DEBUG(x)  fprintf(stderr, x)

struct opts 
{
  char debug;
};

struct slog_disk
{
  int status;
  float pvalue;
};

struct controller 
{
  char ctrl_devicename[20];
  int num_logd_found;
  struct slog_disk log_disk[16];
};



int status_check (struct opts opts) 
{
  int devicefd;
  int ctrl_cntr;
  int logd_cntr;
  ida_ioctl_t io, io2;
  int status, nr_blks, blks_tr;
  float pvalue;
  int counter;
    
  for ( ctrl_cntr=0;
        ctrl_cntr <  ctrls_found_num;
        ctrl_cntr++) {
    
    devicefd = open (controllers[ctrl_cntr], O_RDONLY);
    
    for ( logd_cntr=0;
          logd_cntr < ctrls_found[ctrl_cntr].num_logd_found;
          logd_cntr++) {
      
        memset (&io, 0, sizeof (io));

        io.cmd = SENSE_LOG_DRV_STAT;
        io.unit = logd_cntr  | UNITVALID;
        
        if (ioctl (devicefd, IDAPASSTHRU, &io) < 0)
          {
            perror ("SENSE_LOG_DRV_STAT ioctl");
            return 0;
          }

        status=io.c.sense_log_drv_stat.status;
        
        if ((status == 3) || (status == 5) || (status == 7)) {
          /* is a progress indicator required?
           */
          memset (&io2, 0, sizeof (io));
          
          io2.cmd = ID_LOG_DRV;
          io2.unit = logd_cntr  | UNITVALID;
          
          if (ioctl (devicefd, IDAPASSTHRU, &io2) < 0)
            {
              perror ("ID_LOG_DRV ioctl");
              /* return 0;   no return this isn't fatal for now */
            }
          else 
            {
              nr_blks = io2.c.id_log_drv.nr_blks;
              blks_tr = io.c.sense_log_drv_stat.blks_to_recover;
                  
              pvalue = ((float)(nr_blks - blks_tr)/(float)nr_blks) * 100;
            }
        }
        else {
          pvalue = 0.0;
        }

        if (opts.debug) {
	  fprintf(stdout, "DEBUG: Status of controller %d unit %d is %d\n", 
		  ctrl_cntr, logd_cntr, status);
          fprintf(stdout, "DEBUG: ");
	  fprintf(stdout, statusstr[status], 
		  ctrl_cntr, logd_cntr, pvalue);
	  fprintf(stdout, "\n");
	}
	
	printf(statusstr[status], ctrl_cntr, logd_cntr, pvalue);

	switch(status)
	  {
	  case 1:
	  case 2:
	  case 6:
	  case 7:
	  case 9:
	    /* CRITICAL */
	    exit_code = 2;
	    break;
	  case 3:
	  case 4:
	  case 5:
	  case 8:
	  case 10:
	  case 11:
	  case 12:
	    /* WARNING (only if not yet at CRITICAL LEVEL) */
	    if (exit_code < 2) exit_code = 1;
	    break;
	  case 0:
	  default:
	    /* do nothing */
	    break;
	  }

	ctrls_found[ctrl_cntr].log_disk[logd_cntr].pvalue = pvalue;
	ctrls_found[ctrl_cntr].log_disk[logd_cntr].status = status;
    }
    close (devicefd);
  }

  return 1;
}

int discover_controllers (struct opts opts)
{
  int cntr;
  int foundone = 0;

  for (cntr = 0; cntr < 8; cntr++)
    {
      /* does this device exist ? */
      if ((access (controllers[cntr], R_OK | F_OK)) == 0)
	{
	  /* it does :) */
	  if (interrogate_controller (opts, cntr))
	    {
	      foundone = 1;
	      if (opts.debug) 
		fprintf (stderr, "DEBUG: %s is a existing controller\n",
			 controllers[cntr]);
	    }
	}
      else if (opts.debug)
	{
	  fprintf (stderr, "DEBUG: Device %s could not be opened\n", controllers[cntr]);
	  perror ("DEBUG: reason");
	}
    }
   return foundone;
}

void boardid2str (unsigned long board_id, char *name)
{
  switch (board_id)
    {
    case 0x0040110E:		/* IDA */
      strcpy (name, "Compaq IDA");
      break;
    case 0x0140110E:		/* IDA-2 */
      strcpy (name, "Compaq IDA-2");
      break;
    case 0x1040110E:		/* IAES */
      strcpy (name, "Compaq IAES");
      break;
    case 0x2040110E:		/* SMART */
      strcpy (name, "Compaq SMART");
      break;
    case 0x3040110E:		/* SMART-2/E */
      strcpy (name, "Compaq SMART-2/E");
      break;
    case 0x40300E11:		/* SMART-2/P or SMART-2DH */
      strcpy (name, "Compaq SMART-2/P (2DH)");
      break;
    case 0x40310E11:		/* SMART-2SL */
      strcpy (name, "Compaq SMART-2SL");
      break;
    case 0x40320E11:		/* SMART-3200 */
      strcpy (name, "Compaq SMART-3200");
      break;
    case 0x40330E11:		/* SMART-3100ES */
      strcpy (name, "Compaq SMART-3100ES");
      break;
    case 0x40340E11:		/* SMART-221 */
      strcpy (name, "Compaq SMART-221");
      break;
    case 0x40400E11:		/* Integrated Array */
      strcpy (name, "Compaq Integrated Array");
      break;
    case 0x40500E11:		/* Smart Array 4200 */
      strcpy (name, "Compaq Smart Array 4200");
      break;
    case 0x40510E11:		/* Smart Array 4250ES */
      strcpy (name, "Compaq Smart Array 4250ES");
      break;
    case 0x40580E11:		/* Smart Array 431 */
      strcpy (name, "Compaq Smart Array 431");
      break;
    default:
      /*
       * Well, its a SMART-2 or better, don't know which
       * kind.
       */
      strcpy (name, "Unknown Controller Type");
    }
}

int interrogate_controller (struct opts opts, int contrnum)
{
  int devicefd;
  ida_ioctl_t io;
  char buffer[30];
  int foundone = 0;
  int cntr;
 
  devicefd = open (controllers[contrnum], O_RDONLY);
  /* no checks, did that before */

  /* clear io */
  memset (&io, 0, sizeof (io));

  io.cmd = ID_CTLR;

  if (ioctl (devicefd, IDAPASSTHRU, &io) < 0)
    {
      if (opts.debug) perror ("DEBUG: ioctl");
      return 0;
    }

  boardid2str (io.c.id_ctlr.board_id, buffer);

  strncpy (ctrls_found[ctrls_found_num].ctrl_devicename, 
	   buffer, 20);

  ctrls_found[ctrls_found_num].num_logd_found = 0;

  for (cntr = 0; cntr < io.c.id_ctlr.nr_drvs; cntr++)
    {
      if (interrogate_logical (opts, devicefd, cntr))
	{
	  /* logical drive found, this could be used later one */
	  foundone = 1;
	}
    }

  switch (ctrls_found[ctrls_found_num].num_logd_found)
    {
    case 0:
      printf("Found a %s with no logical drives.\n", buffer);
      break;
    case 1:
      printf("Found a %s with one Logical drive.\n", buffer,
	ctrls_found[ctrls_found_num].num_logd_found);
      break;
    default:
      printf("Found a %s with %d Logical drives.\n", buffer,
	ctrls_found[ctrls_found_num].num_logd_found);
      break;
    }

  ctrls_found_num++;

  close (devicefd);
  return 1;
}

int interrogate_logical (struct opts opts, int devicefd, int unit_nr)
{
  ida_ioctl_t io;
  ida_ioctl_t io2;
  int nr_blks, blks_tr;

  if (opts.debug) printf ("DEBUG: interrogating unit %d\n", unit_nr);

  memset (&io, 0, sizeof (io));

  io.cmd = ID_LOG_DRV;
  io.unit = unit_nr | UNITVALID;

  if (ioctl (devicefd, IDAPASSTHRU, &io) < 0)
    {
      perror ("FATAL: ID_LOG_DRV ioctl");
      return 0;
    }

  memset (&io2, 0, sizeof (io2));

  io2.cmd = SENSE_LOG_DRV_STAT;
  io2.unit = unit_nr | UNITVALID;

  if (ioctl (devicefd, IDAPASSTHRU, &io2) < 0)
    {
      perror ("FATAL: SENSE_LOG_DRV_STAT ioctl");
      return 0;
    }
  
  ctrls_found[ctrls_found_num].num_logd_found++;
  /*  ctrls_found[ctrls_found_num].log_disk[unit_nr].status =
   * io2.c.sense_log_drv_stat.status;

   * nr_blks = io2.c.id_log_drv.nr_blks;
   * blks_tr = io.c.sense_log_drv_stat.blks_to_recover;
   * ctrls_found[ctrls_found_num].log_disk[unit_nr].pvalue =
   *  ((float)(nr_blks - blks_tr)/(float)nr_blks) * 100;
   */
  ctrls_found[ctrls_found_num].log_disk[unit_nr].status = 0;
  ctrls_found[ctrls_found_num].log_disk[unit_nr].pvalue = 0;

  return 1;
}


void print_usage() 
{
  printf("cpqarrayd [options]\n");
  printf("   -h         prints this text\n");
  printf("   -d         enables debugging\n");
}


int main(int argc, char *argv[]) 
{
  char option;
  struct opts opts; /* commandline options */
  
  memset(&opts, 0, sizeof(struct opts));
  
  /* check options */
  while ((option = getopt (argc, argv, "dh:")) != EOF)
    {
      switch (option)
        {
	case 'd':
	  opts.debug = 1;
	  break;
	case '?':
	case 'h':
	default:
	  print_usage();
	  exit(0);
	  break;
	}
    }
  
  /* Check for existance of array controllers */
  if (!discover_controllers(opts)) {
    printf("No array controller found!\n\n");
    exit(1);
  }

  status_check(opts);

  return exit_code;
}
