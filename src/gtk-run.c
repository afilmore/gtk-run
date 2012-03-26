/***********************************************************************************************************************
 * gtk-run.c: Little application launcher
 * Copyright (C) 2006 Hong Jen Yee (PCMan) pcman.tw(AT)gmail.com
 * Copyright (C) 2012 Axel FILMORE <axel.filmore@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 * 
 * This program was originally created by Hong Jen Yee aka PCMan.
 * 
 **********************************************************************************************************************/

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>

//#include "misc.h"
#include <menu-cache.h>

static GtkWidget* win = NULL; /* the run dialog */
static MenuCache* menu_cache = NULL;
static GSList* app_list = NULL; /* all known apps in menu cache */
static gpointer reload_notify_id = NULL;

typedef struct _ThreadData
{
    gboolean cancel; /* is the loading cancelled */
    GSList* files; /* all executable files found */
    GtkEntry* entry;
}ThreadData;

static ThreadData* thread_data = NULL; /* thread data used to load availble programs in PATH */


/***********************************************************************************************************************
 * From lxpanel misc.c
 *
 **********************************************************************************************************************/
void show_error( GtkWindow* parent_win, const char* msg )
{
    GtkWidget* dlg = gtk_message_dialog_new( parent_win,
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK, "%s", msg );
    gtk_dialog_run( (GtkDialog*)dlg );
    gtk_widget_destroy( dlg );
}

/* Try to load an icon from a named file via the freedesktop.org data directories path.
 * http://standards.freedesktop.org/basedir-spec/basedir-spec-0.6.html */
static GdkPixbuf * load_icon_file(const char * file_name, int height, int width)
{
    GdkPixbuf * icon = NULL;
    const gchar ** dirs = (const gchar **) g_get_system_data_dirs();
    const gchar ** dir;
    for (dir = dirs; ((*dir != NULL) && (icon == NULL)); dir++)
    {
        char * file_path = g_build_filename(*dir, "pixmaps", file_name, NULL);
        icon = gdk_pixbuf_new_from_file_at_scale(file_path, height, width, TRUE, NULL);
        g_free(file_path);
    }
    return icon;
}

/* Try to load an icon from the current theme. */
static GdkPixbuf * load_icon_from_theme(GtkIconTheme * theme, const char * icon_name, int width, int height)
{
    GdkPixbuf * icon = NULL;

    /* Look up the icon in the current theme. */
    GtkIconInfo * icon_info = gtk_icon_theme_lookup_icon(theme, icon_name, height, GTK_ICON_LOOKUP_USE_BUILTIN);
    if (icon_info != NULL)
    {
        /* If that succeeded, get the filename of the icon.
         * If that succeeds, load the icon from the specified file.
         * Otherwise, try to get the builtin icon. */
        const char * file = gtk_icon_info_get_filename(icon_info);
        if (file != NULL)
            icon = gdk_pixbuf_new_from_file(file, NULL);
        else
        {
            icon = gtk_icon_info_get_builtin_pixbuf(icon_info);
            g_object_ref(icon);
        }
        gtk_icon_info_free(icon_info);

        /* If the icon is not sized properly, take a trip through the scaler.
         * The lookup above takes the desired size, so we get the closest result possible. */
        if (icon != NULL)
        {
            if ((height != gdk_pixbuf_get_height(icon)) || (width != gdk_pixbuf_get_width(icon)))
            {
                /* Handle case of unspecified width; gdk_pixbuf_scale_simple does not. */
                if (width < 0)
                {
                    int pixbuf_width = gdk_pixbuf_get_width(icon);
                    int pixbuf_height = gdk_pixbuf_get_height(icon);
                    width = height * pixbuf_width / pixbuf_height;
                }
                GdkPixbuf * scaled = gdk_pixbuf_scale_simple(icon, width, height, GDK_INTERP_BILINEAR);
                g_object_unref(icon);
                icon = scaled;
            }
        }
    }
    return icon;
}

GdkPixbuf * gtkrun_load_icon(const char * name, int width, int height, gboolean use_fallback)
{
    GdkPixbuf * icon = NULL;

    if (name != NULL)
    {
        if (g_path_is_absolute(name))
        {
            /* Absolute path. */
            icon = gdk_pixbuf_new_from_file_at_scale(name, width, height, TRUE, NULL);
        }
        else
        {
            /* Relative path. */
            GtkIconTheme * theme = gtk_icon_theme_get_default();
            char * suffix = strrchr(name, '.');
            if ((suffix != NULL)
            && ((g_strcasecmp(&suffix[1], "png") == 0)
              || (g_strcasecmp(&suffix[1], "svg") == 0)
              || (g_strcasecmp(&suffix[1], "xpm") == 0)))
            {
                /* The file extension indicates it could be in the system pixmap directories. */
                icon = load_icon_file(name, width, height);
                if (icon == NULL)
                {
                    /* Not found.
                     * Let's remove the suffix, and see if this name can match an icon in the current icon theme. */
                    char * icon_name = g_strndup(name, suffix - name);
                    icon = load_icon_from_theme(theme, icon_name, width, height);
                    g_free(icon_name);
                }
            }
            else
            {
                 /* No file extension.  It could be an icon name in the icon theme. */
                 icon = load_icon_from_theme(theme, name, width, height);
            }
        }
    }

    /* Fall back to generic icons. */
    if ((icon == NULL) && (use_fallback))
    {
        GtkIconTheme * theme = gtk_icon_theme_get_default();
        icon = load_icon_from_theme(theme, "application-x-executable", width, height);
        if (icon == NULL)
            icon = load_icon_from_theme(theme, "gnome-mime-application-x-executable", width, height);
    }
    return icon;
}
/**********************************************************************************************************************/


static MenuCacheApp* match_app_by_exec(const char* exec)
{
    GSList* l;
    MenuCacheApp* ret = NULL;
    char* exec_path = g_find_program_in_path(exec);
    const char* pexec;
    int path_len, exec_len, len;

    if( ! exec_path )
        return NULL;

    path_len = strlen(exec_path);
    exec_len = strlen(exec);

    for( l = app_list; l; l = l->next )
    {
        MenuCacheApp* app = MENU_CACHE_APP(l->data);
        const char* app_exec = menu_cache_app_get_exec(app);
        if ( ! app_exec)
            continue;
#if 0   /* This is useless and incorrect. */
        /* Dirty hacks to skip sudo programs. This can be a little bit buggy */
        if( g_str_has_prefix(app_exec, "gksu") )
        {
            app_exec += 4;
            if( app_exec[0] == '\0' ) /* "gksu" itself */
                app_exec -= 4;
            else if( app_exec[0] == ' ' ) /* "gksu something..." */
                ++app_exec;
            else if( g_str_has_prefix(app_exec, "do ") ) /* "gksudo something" */
                app_exec += 3;
        }
        else if( g_str_has_prefix(app_exec, "kdesu ") ) /* kdesu */
            app_exec += 6;
#endif

        if( g_path_is_absolute(app_exec) )
        {
            pexec = exec_path;
            len = path_len;
        }
        else
        {
            pexec = exec;
            len = exec_len;
        }

        if( strncmp(app_exec, pexec, len) == 0 )
        {
            /* exact match has the highest priority */
            if( app_exec[len] == '\0' )
            {
                ret = app;
                break;
            }
            /* those matches the pattern: exe_name %F|%f|%U|%u have higher priority */
            if( app_exec[len] == ' ' )
            {
                if( app_exec[len + 1] == '%' )
                {
                    if( strchr( "FfUu", app_exec[len + 2] ) )
                    {
                        ret = app;
                        break;
                    }
                }
                ret = app;
            }
        }
    }

    /* if this is a symlink */
    if( ! ret && g_file_test(exec_path, G_FILE_TEST_IS_SYMLINK) )
    {
        char target[512]; /* FIXME: is this enough? */
        len = readlink( exec_path, target, sizeof(target) - 1);
        if( len > 0 )
        {
            target[len] = '\0';
            ret = match_app_by_exec(target);
            if( ! ret )
            {
                /* FIXME: Actually, target could be relative paths.
                 *        So, actually path resolution is needed here. */
                char* basename = g_path_get_basename(target);
                char* locate = g_find_program_in_path(basename);
                if( locate && strcmp(locate, target) == 0 )
                {
                    ret = match_app_by_exec(basename);
                    g_free(locate);
                }
                g_free(basename);
            }
        }
    }

    g_free(exec_path);
    return ret;
}

static void setup_auto_complete_with_data(ThreadData* data)
{
    GtkListStore* store;
    GSList *l;
    GtkEntryCompletion* comp = gtk_entry_completion_new();
    gtk_entry_completion_set_minimum_key_length( comp, 2 );
    gtk_entry_completion_set_inline_completion( comp, TRUE );
#if GTK_CHECK_VERSION( 2, 8, 0 )
    gtk_entry_completion_set_popup_set_width( comp, TRUE );
    gtk_entry_completion_set_popup_single_match( comp, FALSE );
#endif
    store = gtk_list_store_new( 1, G_TYPE_STRING );

    for( l = data->files; l; l = l->next )
    {
        const char *name = (const char*)l->data;
        GtkTreeIter it;
        gtk_list_store_append( store, &it );
        gtk_list_store_set( store, &it, 0, name, -1 );
    }

    gtk_entry_completion_set_model( comp, (GtkTreeModel*)store );
    g_object_unref( store );
    gtk_entry_completion_set_text_column( comp, 0 );
    gtk_entry_set_completion( (GtkEntry*)data->entry, comp );

    /* trigger entry completion */
    gtk_entry_completion_complete(comp);
    g_object_unref( comp );
}

void thread_data_free(ThreadData* data)
{
    g_slist_foreach(data->files, (GFunc)g_free, NULL);
    g_slist_free(data->files);
    g_slice_free(ThreadData, data);
}

static gboolean on_thread_finished(ThreadData* data)
{
    /* don't setup entry completion if the thread is already cancelled. */
    if( !data->cancel )
        setup_auto_complete_with_data(thread_data);
    thread_data_free(data);
    thread_data = NULL; /* global thread_data pointer */
    return FALSE;
}

static gpointer thread_func(ThreadData* data)
{
    GSList *list = NULL;
    gchar **dirname;
    gchar **dirnames = g_strsplit( g_getenv("PATH"), ":", 0 );

    for( dirname = dirnames; !thread_data->cancel && *dirname; ++dirname )
    {
        GDir *dir = g_dir_open( *dirname, 0, NULL );
        const char *name;
        if( ! dir )
            continue;
        while( !thread_data->cancel && (name = g_dir_read_name(dir)) )
        {
            char* filename = g_build_filename( *dirname, name, NULL );
            if( g_file_test( filename, G_FILE_TEST_IS_EXECUTABLE ) )
            {
                if(thread_data->cancel)
                    break;
                if( !g_slist_find_custom( list, name, (GCompareFunc)strcmp ) )
                    list = g_slist_prepend( list, g_strdup( name ) );
            }
            g_free( filename );
        }
        g_dir_close( dir );
    }
    g_strfreev( dirnames );

    data->files = list;
    /* install an idle handler to free associated data */
    g_idle_add((GSourceFunc)on_thread_finished, data);

    return NULL;
}

static void setup_auto_complete( GtkEntry* entry )
{
    gboolean cache_is_available = FALSE;
    /* FIXME: consider saving the list of commands as on-disk cache. */
    if( cache_is_available )
    {
        /* load cached program list */
    }
    else
    {
        /* load in another working thread */
        thread_data = g_slice_new0(ThreadData); /* the data will be freed in idle handler later. */
        thread_data->entry = entry;
        
        // TODO: use g_threa_new ()
        g_thread_create((GThreadFunc)thread_func, thread_data, FALSE, NULL);
    }
}

static void reload_apps(MenuCache* cache, gpointer user_data)
{
    g_debug("reload apps!");
    if(app_list)
    {
        g_slist_foreach(app_list, (GFunc)menu_cache_item_unref, NULL);
        g_slist_free(app_list);
    }
    app_list = (GSList*)menu_cache_list_all_apps(cache);
}

static void on_response( GtkDialog* dlg, gint response, gpointer user_data )
{
    GtkEntry* entry = (GtkEntry*)user_data;
    if( G_LIKELY(response == GTK_RESPONSE_OK) )
    {
        GError* err = NULL;
        if( !g_spawn_command_line_async( gtk_entry_get_text(entry), &err ) )
        {
            show_error( (GtkWindow*)dlg, err->message );
            g_error_free( err );
            g_signal_stop_emission_by_name( dlg, "response" );
            return;
        }
    }

    /* cancel running thread if needed */
    if( thread_data ) /* the thread is still running */
        thread_data->cancel = TRUE; /* cancel the thread */

    gtk_widget_destroy( (GtkWidget*)dlg );
    win = NULL;

    /* free app list */
    g_slist_foreach(app_list, (GFunc)menu_cache_item_unref, NULL);
    g_slist_free(app_list);
    app_list = NULL;

    /* free menu cache */
    menu_cache_remove_reload_notify(menu_cache, reload_notify_id);
    reload_notify_id = NULL;
    menu_cache_unref(menu_cache);
    menu_cache = NULL;
    
    gtk_main_quit ();
}

static void on_entry_changed( GtkEntry* entry, GtkImage* img )
{
    const char* str = gtk_entry_get_text(entry);
    MenuCacheApp* app = NULL;
    if( str && *str )
        app = match_app_by_exec(str);

    if( app )
    {
        int w, h;
        GdkPixbuf* pix;
        gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &w, &h);
        pix = gtkrun_load_icon(menu_cache_item_get_icon(MENU_CACHE_ITEM(app)), w, h, TRUE);
        gtk_image_set_from_pixbuf(img, pix);
        g_object_unref(pix);
    }
    else
    {
        gtk_image_set_from_stock(img, GTK_STOCK_EXECUTE, GTK_ICON_SIZE_DIALOG);
    }
}

void gtk_run()
{
    GtkWidget *entry, *content, *box, *img;

    
    if( win )
    {
        gtk_window_present(GTK_WINDOW(win));
        return;
    }

    win = gtk_dialog_new_with_buttons( _("Run"),
                                       NULL,
                                       0,
                                       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                       GTK_STOCK_OK, GTK_RESPONSE_OK,
                                       NULL );
    gtk_window_set_skip_taskbar_hint (win, TRUE);
    
    gtk_dialog_set_alternative_button_order((GtkDialog*)win, 
                            GTK_RESPONSE_OK, GTK_RESPONSE_CANCEL, -1);
    gtk_dialog_set_default_response( (GtkDialog*)win, GTK_RESPONSE_OK );
    entry = gtk_entry_new();

    gtk_entry_set_activates_default( (GtkEntry*)entry, TRUE );
    content = gtk_dialog_get_content_area (GTK_DIALOG (win));
    gtk_box_pack_start( (GtkBox*)content,
                         gtk_label_new(_("Enter the command you want to execute:")),
                         FALSE, FALSE, 8 );
    box = gtk_box_new( FALSE, 2 );
    img = gtk_image_new_from_stock( GTK_STOCK_EXECUTE, GTK_ICON_SIZE_DIALOG );
    gtk_box_pack_start( (GtkBox*)content, img,
                         FALSE, FALSE, 4 );
    gtk_box_pack_start( (GtkBox*)box, entry, TRUE, TRUE, 4 );
    gtk_box_pack_start( (GtkBox*)content,
                         box, FALSE, FALSE, 8 );
    g_signal_connect( win, "response", G_CALLBACK(on_response), entry );
    gtk_window_set_position( (GtkWindow*)win, GTK_WIN_POS_CENTER );
    gtk_window_set_default_size( (GtkWindow*)win, 360, -1 );
    gtk_widget_show_all( win );

    setup_auto_complete( (GtkEntry*)entry );
    gtk_widget_show( win );

    g_signal_connect(entry ,"changed", G_CALLBACK(on_entry_changed), img);

    /* get all apps */
    menu_cache = menu_cache_lookup_sync("/etc/xdg/lubuntu/menus/lxde-applications.menu");
    if( menu_cache )
    {
        app_list = (GSList*)menu_cache_list_all_apps(menu_cache);
        reload_notify_id = menu_cache_add_reload_notify(menu_cache, (GFunc) reload_apps, NULL);
    } else {
        printf ("error\n");
    }
}

int main(int argc, char *argv[], char *env[])
{
    setlocale(LC_CTYPE, "");

    gtk_init(&argc, &argv);

#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    gtk_run ();
    gtk_main();


    return 0;
}

