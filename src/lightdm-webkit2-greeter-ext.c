/*
 * lightdm-webkit2-greeter-ext.c
 *
 * Copyright © 2014-2016 Antergos Developers <dev@antergos.com>
 *
 * Includes Code Contributed By:
 * Copyright © 2016 Scott Balneaves <sbalneav@ltsp.org>
 *
 * Based on code from lightdm-webkit-greeter:
 * Copyright © 2010-2014 Robert Ancell <robert.ancell@canonical.com>
 *
 * This file is part of lightdm-webkit2-greeter.
 *
 * lightdm-webkit2-greeter is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * lightdm-webkit2-greeter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * The following additional terms are in effect as per Section 7 of the license:
 *
 * The preservation of all legal notices and author attributions in
 * the material or in the Appropriate Legal Notices displayed
 * by works containing it is required.
 *
 * You should have received a copy of the GNU General Public License
 * along with lightdm-webkit2-greeter; If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <config.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <webkit2/webkit-web-extension.h>
#include <webkitdom/webkitdom.h>
#include <JavaScriptCore/JavaScript.h>

#include <lightdm.h>

#include <config.h>

G_MODULE_EXPORT void webkit_web_extension_initialize (WebKitWebExtension * extension);

guint64 page_id = -1;
GKeyFile *keyfile;

#define USER     ((LightDMUser *)     JSObjectGetPrivate (thisObject))
#define LAYOUT   ((LightDMLayout *)   JSObjectGetPrivate (thisObject))
#define SESSION  ((LightDMSession *)  JSObjectGetPrivate (thisObject))
#define GREETER  ((LightDMGreeter *)  JSObjectGetPrivate (thisObject))
#define LANGUAGE ((LightDMLanguage *) JSObjectGetPrivate (thisObject))

#define READONLY    kJSPropertyAttributeReadOnly
#define NONE        kJSPropertyAttributeNone
#define STDARGS     JSContextRef context, JSObjectRef thisObject, JSStringRef propertyName, JSValueRef * exception
#define STDFUNCARGS JSContextRef context, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef * exception

/*
 * Put all our translatable strings up top
 */

#define EXPECTSTRING   _("Expected a string")
#define ARGNOTSUPPLIED _("Argument(s) not supplied")


static JSClassRef
  gettext_class,
  lightdm_greeter_class,
  lightdm_user_class,
  lightdm_language_class,
  lightdm_layout_class,
  lightdm_session_class,
  config_file_class,
  greeter_util_class;


/*
 * Returns either a string or null.
 *
 * When passed a const gchar string, either return a JSValueRef string or,
 * if the string is NULL, a JSValueRef Null reference. Used to ensure functions
 * return proper string args.
 */

static JSValueRef
string_or_null (JSContextRef context, const gchar * str)
{
  JSValueRef result;
  JSStringRef string;

  if (!str)
    {
      return JSValueMakeNull (context);
    }

  string = JSStringCreateWithUTF8CString (str);
  result = JSValueMakeString (context, string);
  JSStringRelease (string);

  return result;
}


/*
 * Makes an Exception.
 *
 * Convert a const string to an exception which can be passed back to webkit.
 */

static void
_mkexception (JSContextRef context, JSValueRef * exception, const gchar * str)
{
  JSValueRef result;
  JSStringRef string = JSStringCreateWithUTF8CString (str);
  JSValueRef exceptionString = JSValueMakeString (context, string);
  JSStringRelease (string);
  result = JSValueToObject (context, exceptionString, exception);

  if (result)
    {
      *exception = result;
    }
}

static JSValueRef
mkexception (JSContextRef context, JSValueRef * exception, const gchar * str)
{
  _mkexception (context, exception, str);
  return JSValueMakeNull (context);
}


/*
 * Converts an argument to a string.
 *
 * Convert a JSValueRef argument to a g_malloc'd gchar string. Calling function
 * is responsible for g_freeing the string.
 */

static gchar *
arg_to_string (JSContextRef context, JSValueRef arg, JSValueRef * exception)
{
  JSStringRef string;
  size_t size;
  gchar *result;

  if (JSValueGetType (context, arg) != kJSTypeString)
    {
      _mkexception (context, exception, EXPECTSTRING);
      return NULL;
    }

  string = JSValueToStringCopy (context, arg, exception);

  if (!string)
    {
      return NULL;
    }

  size = JSStringGetMaximumUTF8CStringSize (string);
  result = g_malloc (size);

  if (!result)
    {
      return NULL;
    }

  JSStringGetUTF8CString (string, result, size);
  JSStringRelease (string);

  return result;
}


/*
 * g_strreplace
 *
 * Replace one substring with another.  NOTE:  This has the side effect
 * of freeing it's passed text.
 */

static gchar *
g_strreplace (gchar * txt, gchar * from, gchar * to)
{
  gchar **split;
  gchar *result;

  split = g_strsplit (txt, from, -1);
  g_free (txt);
  result = g_strjoinv (to, split);
  g_strfreev (split);
  return result;
}


/*
 * Escapes single quote characters in a string.
 *
 * Simple escape function to make sure strings have any/all single
 * quote characters escaped.
 */

static char *
escape (const gchar * text)
{
  gchar *escaped;
  gchar *result;

  /* Make sure all newlines, tabs, etc. are escaped. */
  escaped = g_strescape (text, NULL);

  /* Replace ' with \\' */
  result = g_strreplace (escaped, "'", "\\'");

  return result;
}


/****************************************************************************/
/*                               Callbacks                                  */
/****************************************************************************/


static JSValueRef
get_user_name_cb (STDARGS)
{
  return string_or_null (context, lightdm_user_get_name (USER));
}


static JSValueRef
get_user_real_name_cb (STDARGS)
{
  return string_or_null (context, lightdm_user_get_real_name (USER));
}


static JSValueRef
get_user_display_name_cb (STDARGS)
{
  return string_or_null (context, lightdm_user_get_display_name (USER));
}


static JSValueRef
get_user_home_directory_cb (STDARGS)
{
  return string_or_null (context, lightdm_user_get_home_directory (USER));
}


static JSValueRef
get_user_image_cb (STDARGS)
{
  const gchar *image_uri = lightdm_user_get_image (USER);
  gchar *image_path;
  gint result;

  image_path = g_filename_from_uri (image_uri, NULL, NULL);
  if (image_path)
    {
      result = g_access (image_path, R_OK);
      g_free (image_path);
    }
  else
    {
      result = g_access (image_uri, R_OK);
    }

  if (result)
    {
      /* Couldn't access */
      return JSValueMakeNull (context);
    }
  else
    {
      return string_or_null (context, image_uri);
    }
}


static JSValueRef
get_user_language_cb (STDARGS)
{
  return string_or_null (context, lightdm_user_get_language (USER));
}


static JSValueRef
get_user_layout_cb (STDARGS)
{
  return string_or_null (context, lightdm_user_get_layout (USER));
}


static JSValueRef
get_user_session_cb (STDARGS)
{
  return string_or_null (context, lightdm_user_get_session (USER));
}


static JSValueRef
get_user_logged_in_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_user_get_logged_in (USER));
}


static JSValueRef
get_language_code_cb (STDARGS)
{
  return string_or_null (context, lightdm_language_get_code (LANGUAGE));
}


static JSValueRef
get_language_name_cb (STDARGS)
{
  return string_or_null (context, lightdm_language_get_name (LANGUAGE));
}


static JSValueRef
get_language_territory_cb (STDARGS)
{
  return string_or_null (context, lightdm_language_get_territory (LANGUAGE));
}


static JSValueRef
get_layout_name_cb (STDARGS)
{
  return string_or_null (context, lightdm_layout_get_name (LAYOUT));
}


static JSValueRef
get_layout_short_description_cb (STDARGS)
{
  return string_or_null (context, lightdm_layout_get_short_description (LAYOUT));
}


static JSValueRef
get_layout_description_cb (STDARGS)
{
  return string_or_null (context, lightdm_layout_get_description (LAYOUT));
}


static JSValueRef
get_session_key_cb (STDARGS)
{
  return string_or_null (context, lightdm_session_get_key (SESSION));
}


static JSValueRef
get_session_name_cb (STDARGS)
{
  return string_or_null (context, lightdm_session_get_name (SESSION));
}


static JSValueRef
get_session_comment_cb (STDARGS)
{
  return string_or_null (context, lightdm_session_get_comment (SESSION));
}


static JSValueRef
get_hostname_cb (STDARGS)
{
  return string_or_null (context, lightdm_get_hostname ());
}


static JSValueRef
get_num_users_cb (STDARGS)
{
  return JSValueMakeNumber (context, g_list_length (lightdm_user_list_get_users (lightdm_user_list_get_instance ())));
}


static JSValueRef
get_users_cb (STDARGS)
{

  JSObjectRef array;
  const GList *users, *link;
  guint i, n_users = 0;
  JSValueRef *args;

  users = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
  n_users = g_list_length ((GList *) users);
  args = g_malloc (sizeof (JSValueRef) * (n_users + 1));

  for (i = 0, link = users; link; i++, link = link->next)
    {
      LightDMUser *user = link->data;
      g_object_ref (user);
      args[i] = JSObjectMake (context, lightdm_user_class, user);
    }

  array = JSObjectMakeArray (context, n_users, args, exception);
  g_free (args);

  if (!array)
    {
      return JSValueMakeNull (context);
    }
  else
    {
      return array;
    }
}


static JSValueRef
get_languages_cb (STDARGS)
{

  JSObjectRef array;
  const GList *languages, *link;
  guint i, n_languages = 0;
  JSValueRef *args;

  languages = lightdm_get_languages ();
  n_languages = g_list_length ((GList *) languages);
  args = g_malloc (sizeof (JSValueRef) * (n_languages + 1));

  for (i = 0, link = languages; link; i++, link = link->next)
    {
      LightDMLanguage *language = link->data;
      g_object_ref (language);
      args[i] = JSObjectMake (context, lightdm_language_class, language);
    }

  array = JSObjectMakeArray (context, n_languages, args, exception);
  g_free (args);

  if (!array)
    {
      return JSValueMakeNull (context);
    }
  else
    {
      return array;
    }
}


static JSValueRef
get_language_cb (STDARGS)
{
  return string_or_null (context, lightdm_language_get_name ((LightDMLanguage *) lightdm_get_language ()));
}


static JSValueRef
get_layouts_cb (STDARGS)
{

  JSObjectRef array;
  const GList *layouts, *link;
  guint i, n_layouts = 0;
  JSValueRef *args;

  layouts = lightdm_get_layouts ();
  n_layouts = g_list_length ((GList *) layouts);
  args = g_malloc (sizeof (JSValueRef) * (n_layouts + 1));

  for (i = 0, link = layouts; link; i++, link = link->next)
    {
      LightDMLayout *layout = link->data;
      g_object_ref (layout);
      args[i] = JSObjectMake (context, lightdm_layout_class, layout);
    }

  array = JSObjectMakeArray (context, n_layouts, args, exception);
  g_free (args);

  if (!array)
    {
      return JSValueMakeNull (context);
    }
  else
    {
      return array;
    }
}


static JSValueRef
get_layout_cb (STDARGS)
{
  return string_or_null (context, lightdm_layout_get_name (lightdm_get_layout ()));
}


static bool
set_layout_cb (JSContextRef context,
           JSObjectRef thisObject,
           JSStringRef propertyName,
           JSValueRef value, JSValueRef * exception)
{

  gchar *layout;
  const GList *layouts, *link;
  layout = arg_to_string (context, value, exception);

  if (!layout)
    {
      return false;
    }

  layouts = lightdm_get_layouts ();

  for (link = layouts; link; link = link->next)
    {
      LightDMLayout *currlayout = link->data;

      if (!(g_strcmp0 (lightdm_layout_get_name (currlayout), layout)))
        {
          g_object_ref (currlayout);
          lightdm_set_layout (currlayout);
          break;
        }
    }

  g_free (layout);

  return true;
}


static JSValueRef
get_sessions_cb (STDARGS)
{

  JSObjectRef array;
  const GList *sessions, *link;
  guint i, n_sessions = 0;
  JSValueRef *args;

  sessions = lightdm_get_sessions ();
  n_sessions = g_list_length ((GList *) sessions);
  args = g_malloc (sizeof (JSValueRef) * (n_sessions + 1));

  for (i = 0, link = sessions; link; i++, link = link->next)
    {
      LightDMSession *session = link->data;
      g_object_ref (session);

      args[i] = JSObjectMake (context, lightdm_session_class, session);
    }

  array = JSObjectMakeArray (context, n_sessions, args, exception);
  g_free (args);

  if (!array)
    {
      return JSValueMakeNull (context);
    }
  else
    {
      return array;
    }
}


static JSValueRef
get_default_session_cb (STDARGS)
{
  return string_or_null (context, lightdm_greeter_get_default_session_hint (GREETER));
}


static JSValueRef
get_lock_hint_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_greeter_get_lock_hint (GREETER));
}


static JSValueRef
get_autologin_timeout_cb (STDARGS)
{
  return JSValueMakeNumber (context, lightdm_greeter_get_autologin_timeout_hint (GREETER));
}


static JSValueRef
cancel_autologin_cb (STDFUNCARGS)
{

  lightdm_greeter_cancel_autologin (GREETER);
  return JSValueMakeNull (context);
}


static JSValueRef
authenticate_cb (STDFUNCARGS)
{
  gchar *name = NULL;

  if (argumentCount > 0)
    {
      name = arg_to_string (context, arguments[0], exception);
    }

  lightdm_greeter_authenticate (GREETER, name);
  g_free (name);

  return JSValueMakeNull (context);
}


static JSValueRef
authenticate_as_guest_cb (STDFUNCARGS)
{
  lightdm_greeter_authenticate_as_guest (GREETER);
  return JSValueMakeNull (context);
}


static JSValueRef
get_hint_cb (STDFUNCARGS)
{
  gchar *hint_name = NULL;
  JSValueRef result;

  if (argumentCount != 1)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  hint_name = arg_to_string (context, arguments[0], exception);

  if (!hint_name)
    {
      return JSValueMakeNull (context);
    }

  result = string_or_null (context, lightdm_greeter_get_hint (GREETER, hint_name));
  g_free (hint_name);

  return result;
}


static JSValueRef
respond_cb (STDFUNCARGS)
{
  gchar *response = NULL;

  if (argumentCount != 1)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  response = arg_to_string (context, arguments[0], exception);

  if (!response)
    {
      return JSValueMakeNull (context);
    }

  lightdm_greeter_respond (GREETER, response);
  g_free (response);

  return JSValueMakeNull (context);
}


static JSValueRef
cancel_authentication_cb (STDFUNCARGS)
{

  lightdm_greeter_cancel_authentication (GREETER);
  return JSValueMakeNull (context);
}


static JSValueRef
get_authentication_user_cb (STDARGS)
{
  return string_or_null (context, lightdm_greeter_get_authentication_user (GREETER));
}


static JSValueRef
get_has_guest_account_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_greeter_get_has_guest_account_hint (GREETER));
}


static JSValueRef
get_hide_users_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_greeter_get_hide_users_hint (GREETER));
}


static JSValueRef
get_select_user_cb (STDARGS)
{
  return string_or_null (context, lightdm_greeter_get_select_user_hint (GREETER));
}


static JSValueRef
get_select_guest_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_greeter_get_select_guest_hint (GREETER));
}


static JSValueRef
get_autologin_user_cb (STDARGS)
{
  return string_or_null (context, lightdm_greeter_get_autologin_user_hint (GREETER));
}


static JSValueRef
get_autologin_guest_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_greeter_get_autologin_guest_hint (GREETER));
}


static JSValueRef
get_is_authenticated_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_greeter_get_is_authenticated (GREETER));
}


static JSValueRef
get_in_authentication_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_greeter_get_in_authentication (GREETER));
}


static JSValueRef
get_can_suspend_cb (STDARGS)
{

  return JSValueMakeBoolean (context, lightdm_get_can_suspend ());
}


static JSValueRef
suspend_cb (STDFUNCARGS)
{

  lightdm_suspend (NULL);
  return JSValueMakeNull (context);
}


static JSValueRef
get_can_hibernate_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_get_can_hibernate ());
}


static JSValueRef
hibernate_cb (STDFUNCARGS)
{

  lightdm_hibernate (NULL);
  return JSValueMakeNull (context);
}


static JSValueRef
get_can_restart_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_get_can_restart ());
}


static JSValueRef
restart_cb (STDFUNCARGS)
{

  lightdm_restart (NULL);
  return JSValueMakeNull (context);
}


static JSValueRef
get_can_shutdown_cb (STDARGS)
{
  return JSValueMakeBoolean (context, lightdm_get_can_shutdown ());
}


static JSValueRef
shutdown_cb (STDFUNCARGS)
{
  lightdm_shutdown (NULL);
  return JSValueMakeNull (context);
}


static JSValueRef
start_session_sync_cb (STDFUNCARGS)
{
  gchar *session = NULL;
  gboolean result;
  GError *err = NULL;

  if (argumentCount >= 1)
    {
      session = arg_to_string (context, arguments[0], exception);
    }

  result = lightdm_greeter_start_session_sync (GREETER, session, &err);
  g_free (session);

  if (err)
    {
      _mkexception (context, exception, err->message);
      g_error_free (err);
    }

  return JSValueMakeBoolean (context, result);
}


static JSValueRef
set_language_cb (STDFUNCARGS)
{
  gchar *language = NULL;

  if (argumentCount != 1)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  language = arg_to_string (context, arguments[0], exception);

  if (!language)
    {
      return JSValueMakeNull (context);
    }

  lightdm_greeter_set_language (GREETER, language);

  g_free (language);

  return JSValueMakeNull (context);
}


static JSValueRef
gettext_cb (STDFUNCARGS)
{
  gchar *string = NULL;
  JSValueRef result;

  if (argumentCount != 1)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  string = arg_to_string (context, arguments[0], exception);

  if (!string)
    {
      return JSValueMakeNull (context);
    }

  result = string_or_null (context, gettext (string));
  g_free (string);

  return result;
}


static JSValueRef
ngettext_cb (STDFUNCARGS)
{
  gchar *string = NULL, *plural_string = NULL;
  unsigned int n = 0;
  JSValueRef result;

  if (argumentCount != 3)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  string = arg_to_string (context, arguments[0], exception);

  if (!string)
    {
      return JSValueMakeNull (context);
    }

  plural_string = arg_to_string (context, arguments[1], exception);

  if (!plural_string)
    {
      return JSValueMakeNull (context);
    }

  n = JSValueToNumber (context, arguments[2], exception);
  result = string_or_null (context, ngettext (string, plural_string, n));

  g_free (string);
  g_free (plural_string);

  return result;
}


/*
 * Gets a key's value from config file.
 *
 * Returns value as a string.
 */

static JSValueRef
get_conf_str_cb (STDFUNCARGS)
{
  gchar *section, *key, *value;
  GError *err = NULL;
  JSValueRef result;

  if (argumentCount != 2)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  section = arg_to_string (context, arguments[0], exception);
  if (!section)
    {
      return JSValueMakeNull (context);
    }

  key = arg_to_string (context, arguments[1], exception);
  if (!key)
    {
      return JSValueMakeNull (context);
    }

  value = g_key_file_get_string (keyfile, section, key, &err);

  if (err)
    {
      _mkexception (context, exception, err->message);
      g_error_free (err);
      return JSValueMakeNull (context);
    }

  result = string_or_null (context, value);

  g_free (value);

  return result;
}


/*
 * Gets a key's value from config file.
 *
 * Returns value as a number.
 */

static JSValueRef
get_conf_num_cb (STDFUNCARGS)
{
  gchar *section, *key;
  gint value;
  GError *err = NULL;

  if (argumentCount != 2)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  section = arg_to_string (context, arguments[0], exception);
  if (!section)
    {
      return JSValueMakeNull (context);
    }

  key = arg_to_string (context, arguments[1], exception);
  if (!key)
    {
      return JSValueMakeNull (context);
    }

  value = g_key_file_get_integer (keyfile, section, key, &err);

  if (err)
    {
      _mkexception (context, exception, err->message);
      g_error_free (err);
      return JSValueMakeNull (context);
    }

  return JSValueMakeNumber (context, value);
}


/*
 * Gets a key's value from config file.
 *
 * Returns value as a bool.
 */

static JSValueRef
get_conf_bool_cb (STDFUNCARGS)
{
  gchar *section, *key;
  gboolean value;
  GError *err = NULL;

  if (argumentCount != 2)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  section = arg_to_string (context, arguments[0], exception);
  if (!section)
    {
      return JSValueMakeNull (context);
    }

  key = arg_to_string (context, arguments[1], exception);
  if (!key)
    {
      return JSValueMakeNull (context);
    }

  value = g_key_file_get_boolean (keyfile, section, key, &err);

  if (err)
    {
      _mkexception (context, exception, err->message);
      g_error_free (err);
      return JSValueMakeNull (context);
    }

  return JSValueMakeBoolean (context, value);
}


static JSValueRef
get_dirlist_cb (STDFUNCARGS)
{
  JSObjectRef array;
  guint n_entries = 0;
  JSValueRef *args = NULL;
  GDir *dir;
  gchar *path, *fullpath;
  const gchar *dirent;
  GError *err = NULL;

  if (argumentCount != 1)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  path = arg_to_string (context, arguments[0], exception);
  if (!path)
    {
      return JSValueMakeNull (context);
    }

  dir = g_dir_open (path, 0, &err);

  if (err)
    {
      _mkexception (context, exception, err->message);
      g_error_free (err);
      return JSValueMakeNull (context);
    }

  /*
   * Create the list of the directory entries
   */

  while ((dirent = g_dir_read_name (dir)))
    {
      n_entries++;
      args = g_realloc (args, sizeof (JSValueRef) * (n_entries + 1));
      fullpath = g_build_filename (path, dirent, NULL);    /* Give theme developer full pathname */
      args[(n_entries - 1)] = string_or_null (context, fullpath);
      g_free (fullpath);
    }

  g_dir_close (dir);

  array = JSObjectMakeArray (context, n_entries, args, exception);
  g_free (args);
  if (!array)
    {
      return JSValueMakeNull (context);
    }
  else
    {
      return array;
    }
}


static JSValueRef
txt2html_cb (STDFUNCARGS)
{
  gchar *txt;
  JSValueRef result;

  if (argumentCount != 1)
    {
      return mkexception (context, exception, ARGNOTSUPPLIED);
    }

  txt = arg_to_string (context, arguments[0], exception);
  if (!txt)
    {
      return JSValueMakeNull (context);
    }

  /* Replace & with &amp; */
  /* Replace " with &quot; */
  /* Replace < with &lt; */
  /* Replace > with &gt; */
  /* Replace newlines with <br> */

  txt = g_strreplace (txt, "&", "&amp;");
  txt = g_strreplace (txt, "\"", "&quot;");
  txt = g_strreplace (txt, "<", "&lt;");
  txt = g_strreplace (txt, ">", "&gt;");
  txt = g_strreplace (txt, "\n", "<br>");

  result = string_or_null (context, txt);
  g_free (txt);

  return result;
}


static const JSStaticValue lightdm_user_values[] = {
  {"name",                get_user_name_cb,                NULL, READONLY},
  {"real_name",           get_user_real_name_cb,           NULL, READONLY},
  {"display_name",        get_user_display_name_cb,        NULL, READONLY},
  {"home_directory",      get_user_home_directory_cb,      NULL, READONLY},
  {"image",               get_user_image_cb,               NULL, READONLY},
  {"language",            get_user_language_cb,            NULL, READONLY},
  {"layout",              get_user_layout_cb,              NULL, READONLY},
  {"session",             get_user_session_cb,             NULL, READONLY},
  {"logged_in",           get_user_logged_in_cb,           NULL, READONLY},
  {NULL,                  NULL,                            NULL, 0}
};

static const JSStaticValue lightdm_language_values[] = {
  {"code",                get_language_code_cb,            NULL, READONLY},
  {"name",                get_language_name_cb,            NULL, READONLY},
  {"territory",           get_language_territory_cb,       NULL, READONLY},
  {NULL,                  NULL,                            NULL, 0}
};

static const JSStaticValue lightdm_layout_values[] = {
  {"name",                get_layout_name_cb,              NULL, READONLY},
  {"short_description",   get_layout_short_description_cb, NULL, READONLY},
  {"description",         get_layout_description_cb,       NULL, READONLY},
  {NULL,                  NULL,                            NULL, 0}
};

static const JSStaticValue lightdm_session_values[] = {
  {"key",                 get_session_key_cb,              NULL, READONLY},
  {"name",                get_session_name_cb,             NULL, READONLY},
  {"comment",             get_session_comment_cb,          NULL, READONLY},
  {NULL,                  NULL,                            NULL, 0}
};

static const JSStaticValue lightdm_greeter_values[] = {
  {"hostname",            get_hostname_cb,                 NULL, READONLY},
  {"users",               get_users_cb,                    NULL, READONLY},
  {"language",            get_language_cb,                 NULL, READONLY},
  {"languages",           get_languages_cb,                NULL, READONLY},
  {"layouts",             get_layouts_cb,                  NULL, READONLY},
  {"layout",              get_layout_cb,          set_layout_cb, NONE},
  {"sessions",            get_sessions_cb,                 NULL, READONLY},
  {"num_users",           get_num_users_cb,                NULL, READONLY},
  {"default_session",     get_default_session_cb,          NULL, READONLY},
  {"authentication_user", get_authentication_user_cb,      NULL, READONLY},
  {"in_authentication",   get_in_authentication_cb,        NULL, READONLY},
  {"is_authenticated",    get_is_authenticated_cb,         NULL, READONLY},
  {"can_suspend",         get_can_suspend_cb,              NULL, READONLY},
  {"can_hibernate",       get_can_hibernate_cb,            NULL, READONLY},
  {"can_restart",         get_can_restart_cb,              NULL, READONLY},
  {"can_shutdown",        get_can_shutdown_cb,             NULL, READONLY},
  {"lock_hint",           get_lock_hint_cb,                NULL, READONLY},
  {"has_guest_account",   get_has_guest_account_cb,        NULL, READONLY},
  {"hide_users",          get_hide_users_cb,               NULL, READONLY},
  {"select_user",         get_select_user_cb,              NULL, READONLY},
  {"select_guest",        get_select_guest_cb,             NULL, READONLY},
  {"autologin_user",      get_autologin_user_cb,           NULL, READONLY},
  {"autologin_guest",     get_autologin_guest_cb,          NULL, READONLY},
  {"autologin_timeout",   get_autologin_timeout_cb,        NULL, READONLY},
  {NULL,                  NULL,                            NULL, 0}
};

static const JSStaticFunction lightdm_greeter_functions[] = {
  {"cancel_autologin",      cancel_autologin_cb,      READONLY},
  {"authenticate",          authenticate_cb,          READONLY},
  {"authenticate_as_guest", authenticate_as_guest_cb, READONLY},
  {"respond",               respond_cb,               READONLY},
  {"cancel_authentication", cancel_authentication_cb, READONLY},
  {"suspend",               suspend_cb,               READONLY},
  {"hibernate",             hibernate_cb,             READONLY},
  {"restart",               restart_cb,               READONLY},
  {"shutdown",              shutdown_cb,              READONLY},
  {"set_language",          set_language_cb,          READONLY},
  {"start_session_sync",    start_session_sync_cb,    READONLY},
  {"get_hint",              get_hint_cb,              READONLY},
  {NULL,                    NULL,                     0}
};

static const JSStaticFunction gettext_functions[] = {
  {"gettext",               gettext_cb,               READONLY},
  {"ngettext",              ngettext_cb,              READONLY},
  {NULL,                    NULL,                     0}
};


static const JSStaticFunction config_file_functions[] = {
  {"get_str",               get_conf_str_cb,          READONLY},
  {"get_num",               get_conf_num_cb,          READONLY},
  {"get_bool",              get_conf_bool_cb,         READONLY},
  {NULL,                    NULL,                     0}
};


static const JSStaticFunction greeter_util_functions[] = {
  {"dirlist",               get_dirlist_cb,           READONLY},
  {"txt2html",              txt2html_cb,              READONLY},
  {NULL,                    NULL,                     0}
};


static const JSClassDefinition lightdm_user_definition = {
  0,                         /* Version       */
  kJSClassAttributeNone,     /* Attributes    */
  "LightDMUser",             /* Class name    */
  NULL,                      /* Parent class  */
  lightdm_user_values,       /* Static values */
};

static const JSClassDefinition lightdm_language_definition = {
  0,                         /* Version       */
  kJSClassAttributeNone,     /* Attributes    */
  "LightDMLanguage",         /* Class name    */
  NULL,                      /* Parent class  */
  lightdm_language_values,   /* Static values */
};

static const JSClassDefinition lightdm_layout_definition = {
  0,                         /* Version       */
  kJSClassAttributeNone,     /* Attributes    */
  "LightDMLayout",           /* Class name    */
  NULL,                      /* Parent class  */
  lightdm_layout_values,     /* Static values */
};

static const JSClassDefinition lightdm_session_definition = {
  0,                         /* Version       */
  kJSClassAttributeNone,     /* Attributes    */
  "LightDMSession",          /* Class name    */
  NULL,                      /* Parent class  */
  lightdm_session_values,    /* Static values */
};

static const JSClassDefinition lightdm_greeter_definition = {
  0,                         /* Version          */
  kJSClassAttributeNone,     /* Attributes       */
  "LightDMGreeter",          /* Class name       */
  NULL,                      /* Parent class     */
  lightdm_greeter_values,    /* Static values    */
  lightdm_greeter_functions, /* Static functions */
};

static const JSClassDefinition gettext_definition = {
  0,                         /* Version          */
  kJSClassAttributeNone,     /* Attributes       */
  "GettextClass",            /* Class name       */
  NULL,                      /* Parent class     */
  NULL,                      /* Static values    */
  gettext_functions,         /* Static functions */
};

static const JSClassDefinition config_file_definition = {
  0,                         /* Version          */
  kJSClassAttributeNone,     /* Attributes       */
  "ConfigFile",              /* Class name       */
  NULL,                      /* Parent class     */
  NULL,                      /* Static values    */
  config_file_functions,     /* Static functions */
};

static const JSClassDefinition greeter_util_definition = {
  0,                         /* Version          */
  kJSClassAttributeNone,     /* Attributes       */
  "GreeterUtil",             /* Class name       */
  NULL,                      /* Parent class     */
  NULL,                      /* Static values    */
  greeter_util_functions,    /* Static functions */
};


static void
window_object_cleared_callback (WebKitScriptWorld * world,
                WebKitWebPage * web_page,
                WebKitFrame * frame, LightDMGreeter * greeter)
{

  JSObjectRef gettext_object, lightdm_greeter_object, config_file_object, greeter_util_object;
  JSGlobalContextRef jsContext;
  JSObjectRef globalObject;
  WebKitDOMDocument *dom_document;
  WebKitDOMDOMWindow *dom_window;
  gchar *message = "LockHint";

  page_id = webkit_web_page_get_id (web_page);

  jsContext = webkit_frame_get_javascript_context_for_script_world (frame, world);
  globalObject = JSContextGetGlobalObject (jsContext);

  gettext_class = JSClassCreate (&gettext_definition);
  lightdm_greeter_class = JSClassCreate (&lightdm_greeter_definition);
  lightdm_user_class = JSClassCreate (&lightdm_user_definition);
  lightdm_language_class = JSClassCreate (&lightdm_language_definition);
  lightdm_layout_class = JSClassCreate (&lightdm_layout_definition);
  lightdm_session_class = JSClassCreate (&lightdm_session_definition);
  config_file_class = JSClassCreate (&config_file_definition);
  greeter_util_class = JSClassCreate (&greeter_util_definition);

  gettext_object = JSObjectMake (jsContext, gettext_class, NULL);

  JSObjectSetProperty (jsContext,
               globalObject,
               JSStringCreateWithUTF8CString ("gettext"),
               gettext_object, NONE, NULL);

  lightdm_greeter_object =
    JSObjectMake (jsContext, lightdm_greeter_class, greeter);

  JSObjectSetProperty (jsContext,
               globalObject,
               JSStringCreateWithUTF8CString ("lightdm"),
               lightdm_greeter_object,
               NONE, NULL);

  config_file_object = JSObjectMake (jsContext, config_file_class, greeter);

  JSObjectSetProperty (jsContext,
               globalObject,
               JSStringCreateWithUTF8CString ("config"),
               config_file_object, NONE, NULL);

  greeter_util_object = JSObjectMake (jsContext, greeter_util_class, NULL);

  JSObjectSetProperty (jsContext,
               globalObject,
               JSStringCreateWithUTF8CString ("greeterutil"),
               greeter_util_object, NONE, NULL);


  /* If the greeter was started as a lock-screen, send message to our UI process. */
  if (lightdm_greeter_get_lock_hint (greeter))
    {
      dom_document = webkit_web_page_get_dom_document (web_page);
      dom_window = webkit_dom_document_get_default_view (dom_document);

      if (dom_window)
        {
          webkit_dom_dom_window_webkit_message_handlers_post_message
            (dom_window, "GreeterBridge", message);
        }
    }

}


static void
show_prompt_cb (LightDMGreeter * greeter,
        const gchar * text,
        LightDMPromptType type, WebKitWebExtension * extension)
{

  WebKitWebPage *web_page;
  WebKitFrame *web_frame;
  JSGlobalContextRef jsContext;
  JSStringRef command;
  gchar *string;
  gchar *etext;
  const gchar *ct = "";

  web_page = webkit_web_extension_get_page (extension, page_id);

  if (web_page)
    {
      web_frame = webkit_web_page_get_main_frame (web_page);
      jsContext = webkit_frame_get_javascript_global_context (web_frame);

      switch (type)
        {
        case LIGHTDM_PROMPT_TYPE_QUESTION:
          ct = "text";
          break;
        case LIGHTDM_PROMPT_TYPE_SECRET:
          ct = "password";
          break;
        }

      etext = escape (text);
      string = g_strdup_printf ("show_prompt('%s', '%s')", etext, ct);
      command = JSStringCreateWithUTF8CString (string);

      JSEvaluateScript (jsContext, command, NULL, NULL, 0, NULL);

      g_free (string);
      g_free (etext);
    }
}


static void
show_message_cb (LightDMGreeter * greeter,
         const gchar * text,
         LightDMMessageType type, WebKitWebExtension * extension)
{

  WebKitWebPage *web_page;
  WebKitFrame *web_frame;
  JSGlobalContextRef jsContext;
  JSStringRef command;
  gchar *etext;
  gchar *string;
  const gchar *mt = "";

  web_page = webkit_web_extension_get_page (extension, page_id);

  if (web_page)
    {
      web_frame = webkit_web_page_get_main_frame (web_page);
      jsContext = webkit_frame_get_javascript_global_context (web_frame);

      switch (type)
        {
        case LIGHTDM_MESSAGE_TYPE_ERROR:
          mt = "error";
          break;
        case LIGHTDM_MESSAGE_TYPE_INFO:
          mt = "info";
          break;
        }

      etext = escape (text);
      string = g_strdup_printf ("show_message('%s', '%s')", etext, mt);
      command = JSStringCreateWithUTF8CString (string);

      JSEvaluateScript (jsContext, command, NULL, NULL, 0, NULL);

      g_free (string);
      g_free (etext);
    }
}


static void
authentication_complete_cb (LightDMGreeter * greeter,
                WebKitWebExtension * extension)
{

  WebKitWebPage *web_page;
  WebKitFrame *web_frame;
  JSGlobalContextRef jsContext;
  JSStringRef command;

  web_page = webkit_web_extension_get_page (extension, page_id);

  if (web_page)
    {
      web_frame = webkit_web_page_get_main_frame (web_page);
      jsContext = webkit_frame_get_javascript_global_context (web_frame);
      command = JSStringCreateWithUTF8CString ("authentication_complete()");

      JSEvaluateScript (jsContext, command, NULL, NULL, 0, NULL);
    }
}


static void
autologin_timer_expired_cb (LightDMGreeter * greeter,
                WebKitWebExtension * extension)
{

  WebKitWebPage *web_page;
  WebKitFrame *web_frame;
  JSGlobalContextRef jsContext;
  JSStringRef command;

  web_page = webkit_web_extension_get_page (extension, page_id);

  if (web_page)
    {
      web_frame = webkit_web_page_get_main_frame (web_page);
      jsContext = webkit_frame_get_javascript_global_context (web_frame);
      command = JSStringCreateWithUTF8CString ("autologin_timer_expired()");

      JSEvaluateScript (jsContext, command, NULL, NULL, 0, NULL);
    }
}


G_MODULE_EXPORT void
webkit_web_extension_initialize (WebKitWebExtension * extension)
{
  LightDMGreeter *greeter = lightdm_greeter_new ();

  g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), extension);
  g_signal_connect (G_OBJECT (greeter), "autologin-timer-expired", G_CALLBACK (autologin_timer_expired_cb), extension);
  g_signal_connect (webkit_script_world_get_default (), "window-object-cleared", G_CALLBACK (window_object_cleared_callback), greeter);
  g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), extension);
  g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), extension);

  lightdm_greeter_connect_sync (greeter, NULL);

  /* load greeter settings from config file */
  keyfile = g_key_file_new ();

  g_key_file_load_from_file (keyfile,
                 CONFIG_DIR "/lightdm-webkit2-greeter.conf",
                 G_KEY_FILE_NONE, NULL);
}

/* vim: set ts=4 sw=4 tw=0 noet : */
