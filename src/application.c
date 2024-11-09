extern struct event_loop g_event_loop;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static OBSERVER_CALLBACK(application_notification_handler) {
  if (CFEqual(notification, kAXCreatedNotification)) {

      /** (rOnInRaJ-dev) This fix, partly fixes the Arc and yabai PIP weird interaction.
* 		  When the Arc browser is present in the current space, clicking on the PIP window behaves as expected.
		  TODO: If there is no Arc browser window, and yabai starts for the first time, then it creates
          the buggy window when you click on the Arc browser for the first time. It behaves normally afterwards. **/

        debug("%s: Received kAXCreatedNotification\n", __FUNCTION__);

        CFTypeRef value;
        AXError result = AXUIElementCopyAttributeValue(element, kAXSubroleAttribute, &value);
        if (result == kAXErrorSuccess && value) {

                CFIndex length = CFStringGetLength(value);
                CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
                char *roleCString = (char *)malloc(maxSize + 1); // +1 for null terminator

                if (roleCString) {
                    // Convert CFStringRef to C string
                    if (CFStringGetCString(value, roleCString, maxSize + 1, kCFStringEncodingUTF8)) {
                        // Compare the C string with the desired value
                        if (strcmp(roleCString, "AXSystemDialog") == 0) {
                            debug("%s: The role is AXSystemDialog\n", __FUNCTION__);
                        } else {

                            debug("%s: The role is not AXSystemDialog\n", __FUNCTION__);
                            event_loop_post(&g_event_loop, WINDOW_CREATED, (void *) CFRetain(element), 0);
                        }
                    } else {
                        debug("%s: Failed to convert role CFString to C string\n", __FUNCTION__);
                    }
                    // Free the allocated memory
                    free(roleCString);
                } else {
                    debug("%s: Failed to allocate memory for role C string\n", __FUNCTION__);
                }
            CFRelease(value);
        } else {
            debug("%s: Failed to get attribute %s\n", __FUNCTION__, CFStringGetCStringPtr(kAXSubroleAttribute, kCFStringEncodingUTF8));
        }

    }
    else if (CFEqual(notification, kAXFocusedWindowChangedNotification)) {
      event_loop_post(&g_event_loop, WINDOW_FOCUSED,
                      (void *)(intptr_t)ax_window_id(element), 0);
    }
    else if (CFEqual(notification, kAXWindowMovedNotification)) {
      event_loop_post(&g_event_loop, WINDOW_MOVED,
                      (void *)(intptr_t)ax_window_id(element), 0);
    }
    else if (CFEqual(notification, kAXWindowResizedNotification)) {
      event_loop_post(&g_event_loop, WINDOW_RESIZED,
                      (void *)(intptr_t)ax_window_id(element), 0);
    }
    else if (CFEqual(notification, kAXTitleChangedNotification)) {
      event_loop_post(&g_event_loop, WINDOW_TITLE_CHANGED,
                      (void *)(intptr_t)ax_window_id(element), 0);
    }
    else if (CFEqual(notification, kAXMenuOpenedNotification)) {
      event_loop_post(&g_event_loop, MENU_OPENED,
                      (void *)(intptr_t)ax_window_id(element), 0);
    }
    else if (CFEqual(notification, kAXMenuClosedNotification)) {
      event_loop_post(&g_event_loop, MENU_CLOSED, NULL, 0);
    }
    else if (CFEqual(notification, kAXWindowMiniaturizedNotification)) {
      event_loop_post(&g_event_loop, WINDOW_MINIMIZED, context, 0);
    }
    else if (CFEqual(notification, kAXWindowDeminiaturizedNotification)) {
      event_loop_post(&g_event_loop, WINDOW_DEMINIMIZED, context, 0);
    }
    else if (CFEqual(notification, kAXUIElementDestroyedNotification)) {
      struct window *window = context;

      //
      // NOTE(koekeishiya): Flag events that are already queued, but not yet
      // processed, so that they will be ignored; the memory we allocated is
      // still valid and will be freed when this event is handled.
      //

      if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, NULL))
        return;

      //
      // NOTE(koekeishiya): Usually we avoid running code off the event-loop
      // thread, however in this case it is fine, as we are only touching fields
      // that never change after the window allocation and creation code.
      //

      window_unobserve(window);

      event_loop_post(&g_event_loop, WINDOW_DESTROYED, window, 0);
    }
  }
#pragma clang diagnostic pop

  bool application_observe(struct application * application) {
    if (AXObserverCreate(application->pid, application_notification_handler,
                         &application->observer_ref) == kAXErrorSuccess) {
      for (int i = 0; i < array_count(ax_application_notification); ++i) {
        AXError result = AXObserverAddNotification(
            application->observer_ref, application->ref,
            ax_application_notification[i], application);
        if (result == kAXErrorSuccess ||
            result == kAXErrorNotificationAlreadyRegistered) {
          application->notification |= 1 << i;
        } else {
          if (result == kAXErrorCannotComplete)
            application->ax_retry = true;
          debug("%s: error '%s' for application '%s' and notification '%s'\n",
                __FUNCTION__, ax_error_str[-result], application->name,
                ax_application_notification_str[i]);
        }
      }

      application->is_observing = true;
      CFRunLoopAddSource(CFRunLoopGetMain(),
                         AXObserverGetRunLoopSource(application->observer_ref),
                         kCFRunLoopDefaultMode);
    }

    return (application->notification & AX_APPLICATION_ALL) ==
           AX_APPLICATION_ALL;
  }

  void application_unobserve(struct application * application) {
    if (application->is_observing) {
      for (int i = 0; i < array_count(ax_application_notification); ++i) {
        if (!(application->notification & (1 << i)))
          continue;

        AXObserverRemoveNotification(application->observer_ref,
                                     application->ref,
                                     ax_application_notification[i]);
        application->notification &= ~(1 << i);
      }

      application->is_observing = false;
      CFRunLoopSourceInvalidate(
          AXObserverGetRunLoopSource(application->observer_ref));
      CFRelease(application->observer_ref);
    }
  }

  uint32_t application_main_window(struct application * application) {
    CFTypeRef window_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXMainWindowAttribute,
                                  &window_ref);
    if (!window_ref)
      return 0;

    uint32_t window_id = ax_window_id(window_ref);
    CFRelease(window_ref);

    return window_id;
  }

  uint32_t application_focused_window(struct application * application) {
    CFTypeRef window_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXFocusedWindowAttribute,
                                  &window_ref);
    if (!window_ref)
      return 0;

    uint32_t window_id = ax_window_id(window_ref);
    CFRelease(window_ref);

    return window_id;
  }

  bool application_is_frontmost(struct application * application) {
    ProcessSerialNumber psn = {0};
    _SLPSGetFrontProcess(&psn);
    return psn_equals(&psn, &application->psn);
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  bool application_is_hidden(struct application * application) {
    return IsProcessVisible(&application->psn) == 0;
  }
#pragma clang diagnostic pop

  CFArrayRef application_window_list(struct application * application) {
    CFTypeRef window_list_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXWindowsAttribute,
                                  &window_list_ref);
    return window_list_ref;
  }

  struct application *application_create(struct process * process) {
    struct application *application = malloc(sizeof(struct application));
    memset(application, 0, sizeof(struct application));

    application->ref = AXUIElementCreateApplication(process->pid);
    application->psn = process->psn;
    application->pid = process->pid;
    application->name = process->name;
    application->is_hidden = application_is_hidden(application);
    SLSGetConnectionIDForPSN(g_connection, &application->psn,
                             &application->connection);

    return application;
  }

  void application_destroy(struct application * application) {
    CFRelease(application->ref);
    free(application);
  }
