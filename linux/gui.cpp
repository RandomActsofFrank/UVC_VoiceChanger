/*
 * Milestone 1 device-selection GUI (GTK3).
 * Select ALSA capture/playback, start/stop stereo pass-through. No DSP.
 */

#include "gui.h"

#include "engine.h"

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>

#include <string>

struct GuiState {
    EngineConfig cfg{};
    PassEngine engine;
    AlsaDeviceList capture{};
    AlsaDeviceList playback{};

    GtkWidget* window = nullptr;
    GtkWidget* input_combo = nullptr;
    GtkWidget* output_combo = nullptr;
    GtkWidget* input_current = nullptr;
    GtkWidget* output_current = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* start_btn = nullptr;
    GtkWidget* stop_btn = nullptr;
    GtkWidget* refresh_btn = nullptr;
};

static GuiState* g_ui = nullptr;

static void free_lists(GuiState* ui) {
    alsa_device_list_free(&ui->capture);
    alsa_device_list_free(&ui->playback);
}

static void fill_combo(GtkComboBoxText* combo, const AlsaDeviceList& list, const char* prefer_id) {
    gtk_combo_box_text_remove_all(combo);
    int select = 0;
    for (int i = 0; i < list.count; i++) {
        gtk_combo_box_text_append(combo, list.items[i].id, list.items[i].label);
        if (prefer_id && prefer_id[0] && strcmp(prefer_id, list.items[i].id) == 0) {
            select = i;
        }
    }
    if (list.count > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), select);
    }
}

static const char* combo_id(GtkComboBoxText* combo) {
    return gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
}

static void update_current_labels(GuiState* ui) {
    const char* in_id = combo_id(GTK_COMBO_BOX_TEXT(ui->input_combo));
    const char* out_id = combo_id(GTK_COMBO_BOX_TEXT(ui->output_combo));
    gtk_label_set_text(GTK_LABEL(ui->input_current), in_id ? in_id : "(none)");
    gtk_label_set_text(GTK_LABEL(ui->output_current), out_id ? out_id : "(none)");
}

static void set_running_ui(GuiState* ui, bool running) {
    gtk_widget_set_sensitive(ui->input_combo, !running);
    gtk_widget_set_sensitive(ui->output_combo, !running);
    gtk_widget_set_sensitive(ui->refresh_btn, !running);
    gtk_widget_set_sensitive(ui->start_btn, !running);
    gtk_widget_set_sensitive(ui->stop_btn, running);
}

static void refresh_devices(GuiState* ui) {
    std::string keep_in = ui->cfg.input_dev;
    std::string keep_out = ui->cfg.output_dev;
    const char* cur_in = combo_id(GTK_COMBO_BOX_TEXT(ui->input_combo));
    const char* cur_out = combo_id(GTK_COMBO_BOX_TEXT(ui->output_combo));
    if (cur_in) {
        keep_in = cur_in;
    }
    if (cur_out) {
        keep_out = cur_out;
    }

    free_lists(ui);
    if (alsa_enumerate_capture(&ui->capture) < 0 || alsa_enumerate_playback(&ui->playback) < 0) {
        gtk_label_set_text(GTK_LABEL(ui->status), alsa_last_error());
        return;
    }
    fill_combo(GTK_COMBO_BOX_TEXT(ui->input_combo), ui->capture, keep_in.c_str());
    fill_combo(GTK_COMBO_BOX_TEXT(ui->output_combo), ui->playback, keep_out.c_str());
    update_current_labels(ui);
    gtk_label_set_text(GTK_LABEL(ui->status), "Devices refreshed. Select input/output, then Start.");
}

static void on_combo_changed(GtkComboBox*, gpointer) {
    if (g_ui) {
        update_current_labels(g_ui);
    }
}

static void on_refresh(GtkButton*, gpointer) {
    if (g_ui && !g_ui->engine.running()) {
        refresh_devices(g_ui);
    }
}

static void on_start(GtkButton*, gpointer) {
    GuiState* ui = g_ui;
    if (!ui || ui->engine.running()) {
        return;
    }
    const char* in_id = combo_id(GTK_COMBO_BOX_TEXT(ui->input_combo));
    const char* out_id = combo_id(GTK_COMBO_BOX_TEXT(ui->output_combo));
    if (!in_id || !out_id) {
        gtk_label_set_text(GTK_LABEL(ui->status), "Select both an input and an output device.");
        return;
    }

    EngineConfig cfg = ui->cfg;
    cfg.rate = 48000;
    cfg.channels = 2;
    snprintf(cfg.input_dev, sizeof(cfg.input_dev), "%s", in_id);
    snprintf(cfg.output_dev, sizeof(cfg.output_dev), "%s", out_id);

    gtk_label_set_text(GTK_LABEL(ui->status), "Starting…");
    if (!ui->engine.start(cfg)) {
        std::string msg = ui->engine.last_error();
        if (msg.empty()) {
            msg = "Failed to start audio engine";
        }
        gtk_label_set_text(GTK_LABEL(ui->status), msg.c_str());
        set_running_ui(ui, false);
        return;
    }
    set_running_ui(ui, true);
    update_current_labels(ui);
    gtk_label_set_text(GTK_LABEL(ui->status), ui->engine.status().c_str());
}

static void on_stop(GtkButton*, gpointer) {
    GuiState* ui = g_ui;
    if (!ui) {
        return;
    }
    ui->engine.stop();
    set_running_ui(ui, false);
    std::string err = ui->engine.last_error();
    if (!err.empty()) {
        gtk_label_set_text(GTK_LABEL(ui->status), err.c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(ui->status), "Stopped.");
    }
}

static gboolean on_tick(gpointer) {
    GuiState* ui = g_ui;
    if (!ui) {
        return G_SOURCE_REMOVE;
    }
    if (ui->engine.running()) {
        const char* in_id = combo_id(GTK_COMBO_BOX_TEXT(ui->input_combo));
        const char* out_id = combo_id(GTK_COMBO_BOX_TEXT(ui->output_combo));
        char buf[320];
        snprintf(buf,
                 sizeof(buf),
                 "Running  48 kHz / S16 / stereo (L→L R→R)  |  periods=%llu  |  %s → %s",
                 (unsigned long long)ui->engine.blocks(),
                 in_id ? in_id : "?",
                 out_id ? out_id : "?");
        gtk_label_set_text(GTK_LABEL(ui->status), buf);
    } else if (gtk_widget_get_sensitive(ui->stop_btn)) {
        set_running_ui(ui, false);
        std::string err = ui->engine.last_error();
        std::string st = ui->engine.status();
        std::string msg = err.empty() ? st : (st + " — " + err);
        if (msg.empty()) {
            msg = "Engine stopped";
        }
        gtk_label_set_text(GTK_LABEL(ui->status), msg.c_str());
    }
    return G_SOURCE_CONTINUE;
}

static void on_destroy(GtkWidget*, gpointer) {
    if (g_ui) {
        g_ui->engine.stop();
        free_lists(g_ui);
    }
    gtk_main_quit();
}

int run_gui(int argc, char** argv, const EngineConfig* defaults) {
    gtk_init(&argc, &argv);

    GuiState ui;
    g_ui = &ui;
    if (defaults) {
        ui.cfg = *defaults;
    } else {
        engine_config_defaults(&ui.cfg);
    }
    ui.cfg.rate = 48000;
    ui.cfg.channels = 2;

    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.window), "UVC Milestone 1 — Stereo Pass-through");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 640, 280);
    gtk_container_set_border_width(GTK_CONTAINER(ui.window), 12);
    g_signal_connect(ui.window, "destroy", G_CALLBACK(on_destroy), nullptr);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(ui.window), grid);

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<b>ALSA stereo pass-through</b>  (48 kHz · 16-bit · 2 ch · no DSP)");
    gtk_grid_attach(GTK_GRID(grid), title, 0, 0, 3, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("INPUT DEVICE"), 0, 1, 1, 1);
    ui.input_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(ui.input_combo, TRUE);
    gtk_grid_attach(GTK_GRID(grid), ui.input_combo, 1, 1, 2, 1);
    g_signal_connect(ui.input_combo, "changed", G_CALLBACK(on_combo_changed), nullptr);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Selected input"), 0, 2, 1, 1);
    ui.input_current = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ui.input_current), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), ui.input_current, 1, 2, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("OUTPUT DEVICE"), 0, 3, 1, 1);
    ui.output_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(ui.output_combo, TRUE);
    gtk_grid_attach(GTK_GRID(grid), ui.output_combo, 1, 3, 2, 1);
    g_signal_connect(ui.output_combo, "changed", G_CALLBACK(on_combo_changed), nullptr);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Selected output"), 0, 4, 1, 1);
    ui.output_current = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ui.output_current), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), ui.output_current, 1, 4, 2, 1);

    ui.refresh_btn = gtk_button_new_with_label("Refresh devices");
    ui.start_btn = gtk_button_new_with_label("Start Audio");
    ui.stop_btn = gtk_button_new_with_label("Stop Audio");
    g_signal_connect(ui.refresh_btn, "clicked", G_CALLBACK(on_refresh), nullptr);
    g_signal_connect(ui.start_btn, "clicked", G_CALLBACK(on_start), nullptr);
    g_signal_connect(ui.stop_btn, "clicked", G_CALLBACK(on_stop), nullptr);
    gtk_grid_attach(GTK_GRID(grid), ui.refresh_btn, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui.start_btn, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui.stop_btn, 2, 5, 1, 1);

    ui.status = gtk_label_new("Loading devices…");
    gtk_label_set_xalign(GTK_LABEL(ui.status), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(ui.status), TRUE);
    gtk_grid_attach(GTK_GRID(grid), ui.status, 0, 6, 3, 1);

    set_running_ui(&ui, false);
    refresh_devices(&ui);
    g_timeout_add(500, on_tick, nullptr);

    gtk_widget_show_all(ui.window);
    gtk_main();
    g_ui = nullptr;
    return 0;
}
