/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes the panel's run-time settings through zmk-feature-custom-settings:
 * its blank timeout and its contrast. A Studio client lists and edits them
 * under the module's own heading, with no page of its own.
 *
 * Register a subsystem to own the namespace, register the values, listen for
 * the settings events, and hand each value to the setter in display_042inch.h.
 * The setters only record and schedule, so nothing here touches the panel or
 * its bus: that stays with the module's work item, which keeps a write clear of
 * the sync device's reads.
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#include <zmk/studio/custom.h>

#include <zmk-display-042inch/display_042inch.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT zmk_display_042inch
#define D042_NODE DT_DRV_INST(0)

/*
 * The subsystem is spelled once, as a token, and the string derived from it:
 * custom-settings takes the string in every setting, ZMK's registration takes
 * the token and stringifies it, and if the two ever disagree every setting is
 * dropped with -ENOENT while both sides look fine. The author prefix keeps the
 * flat global subsystem space apart from other modules' "display".
 */
#define D042_SUBSYSTEM_TOKEN amgskobo__display
#define D042_SUBSYSTEM STRINGIFY(D042_SUBSYSTEM_TOKEN)

/*
 * A fixed "display." prefix rather than the node's devicetree name: there is
 * exactly one zmk,display-042inch node, so there is nothing to tell apart, and a
 * fixed key survives the node being renamed, which would otherwise leave the
 * stored values behind.
 */
#define D042_SETTING_KEY(field) "display." field

/* The stored name is longer than the key: custom-settings saves under
 * "custom_settings/<subsystem>/<key>", against SETTINGS_MAX_NAME_LEN, and only
 * finds out at run time, inside the save path, that it does not fit. */
#define D042_STORAGE_NAME(field) "custom_settings/" D042_SUBSYSTEM "/" D042_SETTING_KEY(field)

BUILD_ASSERT(sizeof(D042_SETTING_KEY("blank_after_s")) <= CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN,
             "setting key \"" D042_SETTING_KEY("blank_after_s") "\" is too long");
BUILD_ASSERT(sizeof(D042_STORAGE_NAME("blank_after_s")) <= SETTINGS_MAX_NAME_LEN,
             "setting key \"" D042_SETTING_KEY("blank_after_s") "\" is too long to store under");

static bool d042_namespace_handler(const zmk_custom_CallRequest *request,
                                 pb_callback_t *encode_response);

/*
 * Registered only to own the namespace: without a subsystem of the same name
 * every setting below is dropped. It answers no calls - the values travel
 * through custom-settings' own RPC - so the handler declines every request.
 */
static struct zmk_rpc_custom_subsystem_meta d042_meta = {
        ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://github.com/amgskobo/zmk-display-0.42inch"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

/* Through a wrapper, so the token expands before `#` stringifies it. */
#define D042_REGISTER_SUBSYSTEM(identifier, meta, handler)                                           \
    ZMK_RPC_CUSTOM_SUBSYSTEM(identifier, meta, handler)

D042_REGISTER_SUBSYSTEM(D042_SUBSYSTEM_TOKEN, &d042_meta, d042_namespace_handler);

static bool d042_namespace_handler(const zmk_custom_CallRequest *request,
                                 pb_callback_t *encode_response) {
    ARG_UNUSED(request);
    ARG_UNUSED(encode_response);

    return false;
}

#define D042_SETTING(name, key, type, default_value, constraint)                                     \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        name, D042_SUBSYSTEM, D042_SETTING_KEY(key), type, default_value,                              \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, constraint);

/* Seconds rather than the Kconfig option's milliseconds: nobody sets a screen
 * timeout to the millisecond, and a small whole number reads better in a
 * client. The defaults are the build's own values, which is also what a
 * client's reset restores. */
#define D042_BLANK_AFTER_MAX_S 600

D042_SETTING(d042_cs_blank_after, "blank_after_s", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
           ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_DISPLAY_042INCH_BLANK_AFTER_MS / 1000),
           ZMK_CUSTOM_SETTING_RANGE_INT32(0, D042_BLANK_AFTER_MAX_S))

/* A node without a contrast property defaults to the middle of the range. */
D042_SETTING(d042_cs_contrast, "contrast", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
           ZMK_CUSTOM_SETTING_VALUE_INT32(DT_PROP_OR(D042_NODE, contrast, 128)),
           ZMK_CUSTOM_SETTING_RANGE_INT32(0, 255))

static bool d042_read_int32(const struct zmk_custom_setting *setting, int32_t *out) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
        return false;
    }
    *out = value.int32_value;
    return true;
}

static void d042_apply_settings(void) {
    int32_t number;

    if (d042_read_int32(&d042_cs_blank_after, &number)) {
        zmk_display_042inch_set_blank_after_ms((uint32_t)CLAMP(number, 0, D042_BLANK_AFTER_MAX_S) *
                                            1000U);
    }
    if (d042_read_int32(&d042_cs_contrast, &number)) {
        zmk_display_042inch_set_contrast((uint8_t)CLAMP(number, 0, 255));
    }
}

static int d042_settings_event_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);

    /* Every setting of every module raises the same event; only ours matter.
     * Re-reading both is simpler than working out which one moved, and the
     * setters ignore a value they already have. */
    if (changed != NULL && changed->setting != &d042_cs_blank_after &&
        changed->setting != &d042_cs_contrast) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    d042_apply_settings();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(display_042inch_custom_settings, d042_settings_event_cb);
ZMK_SUBSCRIPTION(display_042inch_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(display_042inch_custom_settings, zmk_custom_settings_initialized);
