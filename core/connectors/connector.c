/*
 Copyright (c) 2012 Mathieu Laurendeau
 License: GPLv3
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "gimx.h"
#include "connectors/connector.h"
#include "connectors/udp_con.h"
#include "connectors/gpp_con.h"
#include "connectors/usb_con.h"
#include "connectors/usb_spoof.h"
#include <adapter.h>
#include <report.h>
#include "display.h"
#include "stats.h"
#ifndef WIN32
#include "connectors/sixaxis.h"
#include "connectors/btds4.h"
#endif

/*
 * The adapter restarts about 15ms after receiving the reset command.
 * This time is doubled so as to include the reset command transfer duration.
 */
#define ADAPTER_RESET_TIME 30000 //microseconds

int connector_init()
{
  int ret = 0;
  int i;
  s_adapter* adapter;

  for(i=0; i<MAX_CONTROLLERS; ++i)
  {
    adapter = adapter_get(i);
    if(adapter->portname)
    {
      if(!strstr(adapter->portname, "none"))
      {
        if(serial_connect(i, adapter->portname) < 0)
        {
          ret = -1;
        }
        else
        {
          int rtype = adapter_get_type(i);

          if(rtype >= 0)
          {
            printf(_("Detected USB adapter: %s.\n"), controller_get_name(rtype));

            if(adapter->type == C_TYPE_DEFAULT)
            {
              adapter->type = rtype;
            }
            else if(adapter->type != rtype)
            {
              fprintf(stderr, _("Wrong controller type.\n"));
              ret = -1;
            }

            if(adapter->type == C_TYPE_360_PAD)
            {
              if(usb_spoof_spoof_360_controller(i) < 0)
              {
                fprintf(stderr, _("Spoof failed.\n"));
                ret = -1;
              }
            }
            else if(adapter->type == C_TYPE_XONE_PAD)
            {
              /*
               * TODO XONE
               */
            }
            else if(adapter->type == C_TYPE_DS4)
            {
              int status = adapter_get_status(i);

              if(status < 0)
              {
                fprintf(stderr, _("Can't get adapter status.\n"));
                ret = -1;
              }
              else
              {
                if(status == BYTE_STATUS_STARTED)
                {
                  if(adapter_send_reset(i) < 0)
                  {
                    fprintf(stderr, _("Can't reset the adapter.\n"));
                    ret = -1;
                  }
                  else
                  {
                    printf(_("Reset sent to the adapter.\n"));
                    //Leave time for the adapter to reinitialize.
                    usleep(ADAPTER_RESET_TIME);
                  }
                }

                if(ret != -1)
                {
                  ds4_init_report(&adapter->report.value.ds4);

                  if(usb_init(i, DS4_VENDOR, DS4_PRODUCT) < 0)
                  {
                    fprintf(stderr, _("No Dualshock 4 controller was found on USB buses.\n"));
                    ret = -1;
                  }
                  else
                  {
                    if(adapter_send_start(i) < 0)
                    {
                      fprintf(stderr, _("Can't start the adapter.\n"));
                      ret = -1;
                    }
                    else
                    {
                      serial_add_source(i);
                    }
                  }
                }
              }
            }
          }
        }
      }
      if(adapter->type == C_TYPE_DEFAULT)
      {
        fprintf(stderr, _("No controller detected.\n"));
        ret = -1;
      }
    }
    else if(adapter->type == C_TYPE_GPP)
    {
      int rtype = gpp_connect();
      if (rtype < 0)
      {
        fprintf(stderr, _("No controller detected.\n"));
        ret = -1;
      }
      else if(rtype < C_TYPE_MAX)
      {
        printf(_("Detected controller: %s.\n"), controller_get_name(rtype));
        controller_gpp_set_params(rtype);
      }
      else
      {
        fprintf(stderr, _("Unknown GPP controller type.\n"));
        ret = -1;
      }
    }
    else
    {
      if(adapter->dst_ip)
      {
        adapter->dst_fd = udp_connect(adapter->dst_ip, adapter->dst_port, (int *)&adapter->type);
        if(adapter->dst_fd < 0)
        {
          fprintf(stderr, _("Can't connect to port: %d.\n"), adapter->dst_port);
          ret = -1;
        }
        else
        {
          printf(_("Detected controller: %s.\n"), controller_get_name(adapter->type));
        }
      }
#ifndef WIN32
      else if(adapter->bdaddr_dst)
      {
        if(adapter->type == C_TYPE_SIXAXIS
            || adapter->type == C_TYPE_DEFAULT)
        {
          sixaxis_set_dongle(i, adapter->dongle_index);
          sixaxis_set_bdaddr(i, adapter->bdaddr_dst);
          if(sixaxis_connect(i) < 0)
          {
            fprintf(stderr, _("Can't initialize sixaxis.\n"));
            ret = -1;
          }
        }
        else if(adapter->type == C_TYPE_DS4)
        {
          btds4_set_dongle(i, adapter->dongle_index);
          btds4_set_bdaddr(i, adapter->bdaddr_dst);
          if(btds4_init(i) < 0)
          {
            fprintf(stderr, _("Can't initialize btds4.\n"));
            ret = -1;
          }
          ds4_init_report(&adapter->report.value.ds4);
        }
        else
        {
          fprintf(stderr, _("Wrong controller type.\n"));
        }
      }
#endif
      
    }
    if(adapter->src_ip)
    {
      adapter->src_fd = udp_listen(adapter->src_ip, adapter->src_port);
      if(adapter->src_fd < 0)
      {
        fprintf(stderr, _("Can't listen on port: %d.\n"), adapter->src_port);
        ret = -1;
      }
      else
      {
        GE_AddSource(adapter->src_fd, i, adapter_network_read, NULL, adapter_network_close);
      }
    }
  }
  return ret;
}

void connector_clean()
{
  int i;
  s_adapter* adapter;
  for(i=0; i<MAX_CONTROLLERS; ++i)
  {
    adapter = adapter_get(i);
    if(adapter->dst_fd >= 0)
    {
      GE_RemoveSource(adapter->src_fd);
      udp_close(adapter->dst_fd);
    }
#ifndef WIN32
    else if(adapter->bdaddr_dst)
    {
      if(adapter->type == C_TYPE_SIXAXIS
          || adapter->type == C_TYPE_DEFAULT)
      {
        sixaxis_close(i);
      }
      else if(adapter->type == C_TYPE_DS4)
      {
        btds4_close(i);
      }
    }
#endif
    else if(adapter->portname)
    {
      if(adapter->type == C_TYPE_DS4)
      {
        usb_close(i);
        adapter_send_reset(i);
      }
      serial_close(i);
    }
    else if(adapter->type == C_TYPE_GPP)
    {
      gpp_disconnect();
    }
  }
}

int connector_send()
{
  int ret = 0;
  int i;
  s_adapter* adapter;

  for(i=0; i<MAX_CONTROLLERS; ++i)
  {
    adapter = adapter_get(i);

    if (gimx_params.force_updates || adapter->send_command)
    {
      if(adapter->dst_fd >= 0)
      {
        static unsigned char report[sizeof(adapter->axis)+2] = {0xff, sizeof(adapter->axis)};
        memcpy(report+2, adapter->axis, sizeof(adapter->axis));
        ret = udp_send(adapter->dst_fd, report, sizeof(report));
      }
      else
      {
        s_report* report = &adapter->report;
        report->value_len = report_build(adapter->type, adapter->axis, report);

        switch(adapter->type)
        {
#ifndef WIN32
        case C_TYPE_DEFAULT:
          if(adapter->bdaddr_dst)
          {
            ret = sixaxis_send_interrupt(i, &report->value.ds3);
          }
          break;
#endif
        case C_TYPE_SIXAXIS:
          if(adapter->portname)
          {
            ret = serial_send(i, report, 2+report->value_len);
          }
#ifndef WIN32          
          else if(adapter->bdaddr_dst)
          {
            ret = sixaxis_send_interrupt(i, &report->value.ds3);
          }
#endif
          break;
        case C_TYPE_DS4:
          if(adapter->portname)
          {
            report->value.ds4.report_id = 0x01;
            report->value_len = DS4_USB_INTERRUPT_PACKET_SIZE;
            ret = serial_send(i, report, HEADER_SIZE+report->value_len);
          }
#ifndef WIN32
          else if(adapter->bdaddr_dst)
          {
            ret = btds4_send_interrupt(i, &report->value.ds4, adapter->send_command);
          }
#endif
          break;
        case C_TYPE_GPP:
          ret = gpp_send(adapter->axis);
          break;
        default:
          if(adapter->portname)
          {
            if(adapter->type != C_TYPE_PS2_PAD)
            {
              ret = serial_send(i, report, 2+report->value_len);
            }
            else
            {
              ret = serial_send(i, &report->value.ds2, report->value_len);
            }
          }
          break;
        }
      }

      if(gimx_params.curses)
      {
        stats_update(i);
      }

      if (adapter->send_command)
      {
        if(gimx_params.status)
        {
          adapter_dump_state(i);
#ifdef WIN32
          //There is no setlinebuf(stdout) in windows.
          fflush(stdout);
#endif
        }
        if(gimx_params.curses)
        {
          display_run(adapter_get(0)->type, adapter_get(0)->axis);
        }

        adapter->send_command = 0;
      }
    }
  }
  return ret;
}
