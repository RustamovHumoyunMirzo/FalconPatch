#include "utils/inject_profile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (error && error_size > 0) {
        snprintf(error, error_size, "%s", message ? message : "Unknown profile error.");
    }
}

static int copy_text(char *dest, size_t size, const char *value,
                     char *error, size_t error_size) {
    size_t length;

    if (!value) {
        value = "";
    }
    length = strlen(value);
    if (length >= size) {
        set_error(error, error_size, "A profile value exceeds the supported length.");
        return 0;
    }
    memcpy(dest, value, length + 1);
    return 1;
}

static const char *scalar_value(yaml_node_t *node) {
    if (!node || node->type != YAML_SCALAR_NODE) {
        return NULL;
    }
    return (const char *)node->data.scalar.value;
}

static yaml_node_t *mapping_value(yaml_document_t *document, yaml_node_t *mapping,
                                  const char *key) {
    yaml_node_pair_t *pair;

    if (!mapping || mapping->type != YAML_MAPPING_NODE) {
        return NULL;
    }
    for (pair = mapping->data.mapping.pairs.start;
         pair < mapping->data.mapping.pairs.top; pair++) {
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);
        const char *key_value = scalar_value(key_node);
        if (key_value && strcmp(key_value, key) == 0) {
            return yaml_document_get_node(document, pair->value);
        }
    }
    return NULL;
}

static int parse_bool(const char *value, int *result) {
    char lower[16];
    size_t i;

    if (!value || strlen(value) >= sizeof(lower)) {
        return 0;
    }
    for (i = 0; value[i]; i++) {
        lower[i] = (char)tolower((unsigned char)value[i]);
    }
    lower[i] = '\0';
    if (strcmp(lower, "true") == 0 || strcmp(lower, "yes") == 0 || strcmp(lower, "on") == 0 ||
        strcmp(lower, "1") == 0) {
        *result = 1;
        return 1;
    }
    if (strcmp(lower, "false") == 0 || strcmp(lower, "no") == 0 || strcmp(lower, "off") == 0 ||
        strcmp(lower, "0") == 0) {
        *result = 0;
        return 1;
    }
    return 0;
}

static int is_absolute_path(const char *path) {
    if (!path || !path[0]) {
        return 0;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

static void profile_directory(const char *path, char *directory, size_t size) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *end = slash;
    size_t length;

    if (!end || (backslash && backslash > end)) {
        end = backslash;
    }
    if (!end) {
        snprintf(directory, size, ".");
        return;
    }
    length = (size_t)(end - path);
    if (length == 0) {
        length = 1;
    }
    if (length >= size) {
        length = size - 1;
    }
    memcpy(directory, path, length);
    directory[length] = '\0';
}

static int resolve_path(char *dest, size_t size, const char *directory,
                        const char *value, char *error, size_t error_size) {
    int written;

    if (!value || !value[0]) {
        dest[0] = '\0';
        return 1;
    }
    if (is_absolute_path(value)) {
        return copy_text(dest, size, value, error, error_size);
    }
    written = snprintf(dest, size, "%s/%s", directory, value);
    if (written < 0 || (size_t)written >= size) {
        set_error(error, error_size, "A resolved profile path is too long.");
        return 0;
    }
    return 1;
}

void fpatch_profile_init(FpatchInjectProfile *profile) {
    if (!profile) {
        return;
    }
    memset(profile, 0, sizeof(*profile));
    snprintf(profile->strategy, sizeof(profile->strategy), "auto");
    snprintf(profile->bootstrap_language, sizeof(profile->bootstrap_language), "java");
    snprintf(profile->key_alias, sizeof(profile->key_alias), "androiddebugkey");
    snprintf(profile->store_password, sizeof(profile->store_password), "android");
    snprintf(profile->key_password, sizeof(profile->key_password), "android");
}

int fpatch_profile_add_native(FpatchInjectProfile *profile, const char *path,
                              char *error, size_t error_size) {
    FpatchNativeInput *input;

    if (profile->native_count >= FPATCH_MAX_NATIVE_INPUTS) {
        set_error(error, error_size, "Too many native libraries in the injection profile.");
        return 0;
    }
    input = &profile->native[profile->native_count];
    memset(input, 0, sizeof(*input));
    if (!copy_text(input->path, sizeof(input->path), path, error, error_size)) {
        return 0;
    }
    profile->native_count++;
    return 1;
}

int fpatch_profile_add_lua(FpatchInjectProfile *profile, const char *path,
                           int entry, char *error, size_t error_size) {
    FpatchLuaInput *input;

    if (profile->lua_count >= FPATCH_MAX_LUA_INPUTS) {
        set_error(error, error_size, "Too many Lua scripts in the injection profile.");
        return 0;
    }
    input = &profile->lua[profile->lua_count];
    memset(input, 0, sizeof(*input));
    if (!copy_text(input->path, sizeof(input->path), path, error, error_size)) {
        return 0;
    }
    input->entry = entry;
    profile->lua_count++;
    return 1;
}

int fpatch_profile_add_asset(FpatchInjectProfile *profile, const char *path,
                             char *error, size_t error_size) {
    if (profile->asset_count >= FPATCH_MAX_ASSETS) {
        set_error(error, error_size, "Too many assets in the injection profile.");
        return 0;
    }
    if (!copy_text(profile->assets[profile->asset_count], FPATCH_PATH_MAX,
                   path, error, error_size)) {
        return 0;
    }
    profile->asset_count++;
    return 1;
}

int fpatch_profile_add_split(FpatchInjectProfile *profile, const char *path,
                             char *error, size_t error_size) {
    if (profile->split_count >= FPATCH_MAX_SPLITS) {
        set_error(error, error_size, "Too many split APKs in the injection profile.");
        return 0;
    }
    if (!copy_text(profile->splits[profile->split_count], FPATCH_PATH_MAX,
                   path, error, error_size)) {
        return 0;
    }
    profile->split_count++;
    return 1;
}

static int load_scalar_field(yaml_document_t *document, yaml_node_t *root,
                             const char *key, char *dest, size_t size,
                             char *error, size_t error_size) {
    yaml_node_t *node = mapping_value(document, root, key);
    const char *value;

    if (!node) {
        return 1;
    }
    value = scalar_value(node);
    if (!value) {
        set_error(error, error_size, "Expected a scalar profile value.");
        return 0;
    }
    return copy_text(dest, size, value, error, error_size);
}

static int load_path_sequence(yaml_document_t *document, yaml_node_t *root,
                              const char *key, const char *directory,
                              FpatchInjectProfile *profile, int splits,
                              char *error, size_t error_size) {
    yaml_node_t *sequence = mapping_value(document, root, key);
    yaml_node_item_t *item;

    if (!sequence) {
        return 1;
    }
    if (sequence->type != YAML_SEQUENCE_NODE) {
        set_error(error, error_size, "Expected a sequence of file paths.");
        return 0;
    }
    for (item = sequence->data.sequence.items.start;
         item < sequence->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(document, *item);
        const char *value = scalar_value(node);
        char resolved[FPATCH_PATH_MAX];
        if (!value || !resolve_path(resolved, sizeof(resolved), directory, value,
                                    error, error_size)) {
            set_error(error, error_size, "Expected a valid file path in a sequence.");
            return 0;
        }
        if (splits) {
            if (!fpatch_profile_add_split(profile, resolved, error, error_size)) {
                return 0;
            }
        } else if (!fpatch_profile_add_asset(profile, resolved, error, error_size)) {
            return 0;
        }
    }
    return 1;
}

static int load_native_item(yaml_document_t *document, yaml_node_t *node,
                            const char *abi, const char *directory,
                            FpatchInjectProfile *profile,
                            char *error, size_t error_size) {
    FpatchNativeInput *input;
    const char *path;

    if (profile->native_count >= FPATCH_MAX_NATIVE_INPUTS) {
        set_error(error, error_size, "Too many native libraries in the profile.");
        return 0;
    }
    input = &profile->native[profile->native_count];
    memset(input, 0, sizeof(*input));
    if (node->type == YAML_SCALAR_NODE) {
        path = scalar_value(node);
    } else if (node->type == YAML_MAPPING_NODE) {
        path = scalar_value(mapping_value(document, node, "path"));
        if (!load_scalar_field(document, node, "name", input->name, sizeof(input->name), error, error_size) ||
            !load_scalar_field(document, node, "init", input->init, sizeof(input->init), error, error_size) ||
            !load_scalar_field(document, node, "module", input->lua_module, sizeof(input->lua_module), error, error_size)) {
            return 0;
        }
        if (!abi || !abi[0]) {
            const char *item_abi = scalar_value(mapping_value(document, node, "abi"));
            abi = item_abi;
        }
    } else {
        path = NULL;
    }
    if (!path || !path[0]) {
        set_error(error, error_size, "Every native library requires a path.");
        return 0;
    }
    if (!resolve_path(input->path, sizeof(input->path), directory, path, error, error_size) ||
        (abi && !copy_text(input->abi, sizeof(input->abi), abi, error, error_size))) {
        return 0;
    }
    profile->native_count++;
    return 1;
}

static int load_native(yaml_document_t *document, yaml_node_t *root,
                       const char *directory, FpatchInjectProfile *profile,
                       char *error, size_t error_size) {
    yaml_node_t *native = mapping_value(document, root, "native");

    if (!native) {
        return 1;
    }
    if (native->type == YAML_SEQUENCE_NODE) {
        yaml_node_item_t *item;
        for (item = native->data.sequence.items.start;
             item < native->data.sequence.items.top; item++) {
            if (!load_native_item(document, yaml_document_get_node(document, *item),
                                  NULL, directory, profile, error, error_size)) {
                return 0;
            }
        }
        return 1;
    }
    if (native->type == YAML_MAPPING_NODE) {
        yaml_node_pair_t *pair;
        for (pair = native->data.mapping.pairs.start;
             pair < native->data.mapping.pairs.top; pair++) {
            const char *abi = scalar_value(yaml_document_get_node(document, pair->key));
            yaml_node_t *items = yaml_document_get_node(document, pair->value);
            yaml_node_item_t *item;
            if (!abi || !items || items->type != YAML_SEQUENCE_NODE) {
                set_error(error, error_size, "Native ABI entries must contain a sequence.");
                return 0;
            }
            for (item = items->data.sequence.items.start;
                 item < items->data.sequence.items.top; item++) {
                if (!load_native_item(document, yaml_document_get_node(document, *item),
                                      abi, directory, profile, error, error_size)) {
                    return 0;
                }
            }
        }
        return 1;
    }
    set_error(error, error_size, "The native field must be a mapping or sequence.");
    return 0;
}

static int load_lua(yaml_document_t *document, yaml_node_t *root,
                    const char *directory, FpatchInjectProfile *profile,
                    char *error, size_t error_size) {
    yaml_node_t *scripts = mapping_value(document, root, "scripts");
    yaml_node_t *lua = scripts ? mapping_value(document, scripts, "lua") : NULL;
    yaml_node_item_t *item;

    if (!lua) {
        return 1;
    }
    if (lua->type != YAML_SEQUENCE_NODE) {
        set_error(error, error_size, "scripts.lua must be a sequence.");
        return 0;
    }
    for (item = lua->data.sequence.items.start;
         item < lua->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(document, *item);
        FpatchLuaInput *input;
        const char *path;
        const char *entry_value;

        if (profile->lua_count >= FPATCH_MAX_LUA_INPUTS) {
            set_error(error, error_size, "Too many Lua scripts in the profile.");
            return 0;
        }
        input = &profile->lua[profile->lua_count];
        memset(input, 0, sizeof(*input));
        if (node->type == YAML_SCALAR_NODE) {
            path = scalar_value(node);
        } else if (node->type == YAML_MAPPING_NODE) {
            path = scalar_value(mapping_value(document, node, "path"));
            if (!load_scalar_field(document, node, "module", input->module,
                                   sizeof(input->module), error, error_size)) {
                return 0;
            }
            entry_value = scalar_value(mapping_value(document, node, "entry"));
            if (entry_value && !parse_bool(entry_value, &input->entry)) {
                set_error(error, error_size, "Lua entry must be true or false.");
                return 0;
            }
        } else {
            path = NULL;
        }
        if (!path || !resolve_path(input->path, sizeof(input->path), directory,
                                   path, error, error_size)) {
            set_error(error, error_size, "Every Lua script requires a path.");
            return 0;
        }
        profile->lua_count++;
    }
    return 1;
}

static FpatchDexPatchAction dex_patch_action(const char *value) {
    if (!value) {
        return 0;
    }
    if (strcmp(value, "return_true") == 0) {
        return FPATCH_DEX_PATCH_RETURN_TRUE;
    }
    if (strcmp(value, "return_false") == 0) {
        return FPATCH_DEX_PATCH_RETURN_FALSE;
    }
    if (strcmp(value, "return_zero") == 0) {
        return FPATCH_DEX_PATCH_RETURN_ZERO;
    }
    if (strcmp(value, "return_null") == 0) {
        return FPATCH_DEX_PATCH_RETURN_NULL;
    }
    if (strcmp(value, "return_void") == 0) {
        return FPATCH_DEX_PATCH_RETURN_VOID;
    }
    return 0;
}

static int load_dex_patches(yaml_document_t *document, yaml_node_t *root,
                            FpatchInjectProfile *profile,
                            char *error, size_t error_size) {
    yaml_node_t *sequence = mapping_value(document, root, "dex_patches");
    yaml_node_item_t *item;

    if (!sequence) {
        return 1;
    }
    if (sequence->type != YAML_SEQUENCE_NODE) {
        set_error(error, error_size, "dex_patches must be a sequence.");
        return 0;
    }
    for (item = sequence->data.sequence.items.start;
         item < sequence->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(document, *item);
        yaml_node_t *replace;
        FpatchDexPatch *patch;
        const char *target;
        const char *method;
        const char *action;

        if (!node || node->type != YAML_MAPPING_NODE) {
            set_error(error, error_size, "Every dex_patches item must be a mapping.");
            return 0;
        }
        if (profile->dex_patch_count >= FPATCH_MAX_DEX_PATCHES) {
            set_error(error, error_size, "Too many declarative DEX patches.");
            return 0;
        }
        patch = &profile->dex_patches[profile->dex_patch_count];
        memset(patch, 0, sizeof(*patch));
        target = scalar_value(mapping_value(document, node, "target"));
        method = scalar_value(mapping_value(document, node, "method"));
        action = scalar_value(mapping_value(document, node, "action"));
        replace = mapping_value(document, node, "replace_string");
        if (!target || !target[0] ||
            !copy_text(patch->target, sizeof(patch->target), target,
                       error, error_size)) {
            set_error(error, error_size, "Every DEX patch requires a target class.");
            return 0;
        }
        if (replace) {
            const char *from;
            const char *to;
            if (method || action || replace->type != YAML_MAPPING_NODE) {
                set_error(error, error_size,
                          "replace_string cannot be combined with method/action.");
                return 0;
            }
            from = scalar_value(mapping_value(document, replace, "from"));
            to = scalar_value(mapping_value(document, replace, "to"));
            if (!from || !to || !from[0] || strcmp(from, to) == 0 ||
                !copy_text(patch->string_from, sizeof(patch->string_from), from,
                           error, error_size) ||
                !copy_text(patch->string_to, sizeof(patch->string_to), to,
                           error, error_size)) {
                set_error(error, error_size,
                          "replace_string requires distinct, non-empty from/to values.");
                return 0;
            }
            patch->action = FPATCH_DEX_PATCH_REPLACE_STRING;
        } else {
            const char *open = method ? strchr(method, '(') : NULL;
            const char *close = open ? strchr(open, ')') : NULL;
            FpatchDexPatchAction parsed_action = dex_patch_action(action);
            if (!method || !method[0] || !open || open == method || !close ||
                !close[1] || method[0] == '<' || !parsed_action ||
                !copy_text(patch->method, sizeof(patch->method), method,
                           error, error_size)) {
                set_error(error, error_size,
                          "Method DEX patches require method and a supported return action.");
                return 0;
            }
            patch->action = parsed_action;
        }
        profile->dex_patch_count++;
    }
    return 1;
}

static int load_signing(yaml_document_t *document, yaml_node_t *root,
                        const char *directory, FpatchInjectProfile *profile,
                        char *error, size_t error_size) {
    yaml_node_t *signing = mapping_value(document, root, "signing");
    const char *keystore;
    const char *no_sign;

    if (!signing) {
        return 1;
    }
    if (signing->type != YAML_MAPPING_NODE) {
        set_error(error, error_size, "signing must be a mapping.");
        return 0;
    }
    keystore = scalar_value(mapping_value(document, signing, "keystore"));
    if (keystore && !resolve_path(profile->keystore, sizeof(profile->keystore),
                                  directory, keystore, error, error_size)) {
        return 0;
    }
    if (!load_scalar_field(document, signing, "alias", profile->key_alias,
                           sizeof(profile->key_alias), error, error_size) ||
        !load_scalar_field(document, signing, "store_password", profile->store_password,
                           sizeof(profile->store_password), error, error_size) ||
        !load_scalar_field(document, signing, "key_password", profile->key_password,
                           sizeof(profile->key_password), error, error_size)) {
        return 0;
    }
    no_sign = scalar_value(mapping_value(document, signing, "disabled"));
    if (no_sign && !parse_bool(no_sign, &profile->no_sign)) {
        set_error(error, error_size, "signing.disabled must be true or false.");
        return 0;
    }
    return 1;
}

int fpatch_profile_load(const char *path, FpatchInjectProfile *profile,
                        char *error, size_t error_size) {
    FILE *file;
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    char directory[FPATCH_PATH_MAX];
    char source_value[FPATCH_PATH_MAX] = "";
    char output_value[FPATCH_PATH_MAX] = "";
    char artifacts_value[FPATCH_PATH_MAX] = "";
    const char *random_value;
    int parser_ready = 0;
    int document_ready = 0;
    int ok = 0;

    if (!path || !profile) {
        set_error(error, error_size, "A profile path is required.");
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "Cannot open profile: %s", path);
        return 0;
    }
    if (!yaml_parser_initialize(&parser)) {
        fclose(file);
        set_error(error, error_size, "Cannot initialize the YAML parser.");
        return 0;
    }
    parser_ready = 1;
    yaml_parser_set_input_file(&parser, file);
    if (!yaml_parser_load(&parser, &document)) {
        snprintf(error, error_size, "Profile parse error at line %lu: %s",
                 (unsigned long)parser.problem_mark.line + 1,
                 parser.problem ? parser.problem : "invalid JSON/YAML");
        goto cleanup;
    }
    document_ready = 1;
    root = yaml_document_get_root_node(&document);
    if (!root || root->type != YAML_MAPPING_NODE) {
        set_error(error, error_size, "The profile root must be a mapping/object.");
        goto cleanup;
    }

    profile_directory(path, directory, sizeof(directory));
    if (!load_scalar_field(&document, root, "name", profile->profile_name,
                           sizeof(profile->profile_name), error, error_size) ||
        !load_scalar_field(&document, root, "source", source_value,
                           sizeof(source_value), error, error_size) ||
        !load_scalar_field(&document, root, "output", output_value,
                           sizeof(output_value), error, error_size) ||
        !load_scalar_field(&document, root, "artifacts", artifacts_value,
                           sizeof(artifacts_value), error, error_size) ||
        !load_scalar_field(&document, root, "strategy", profile->strategy,
                           sizeof(profile->strategy), error, error_size) ||
        !load_scalar_field(&document, root, "bootstrap_language", profile->bootstrap_language,
                           sizeof(profile->bootstrap_language), error, error_size) ||
        !resolve_path(profile->source, sizeof(profile->source), directory,
                      source_value, error, error_size) ||
        !resolve_path(profile->output, sizeof(profile->output), directory,
                      output_value, error, error_size) ||
        !resolve_path(profile->artifacts, sizeof(profile->artifacts), directory,
                      artifacts_value, error, error_size)) {
        goto cleanup;
    }

    random_value = scalar_value(mapping_value(&document, root, "random_libname"));
    if (random_value && !parse_bool(random_value, &profile->random_libname)) {
        set_error(error, error_size, "random_libname must be true or false.");
        goto cleanup;
    }
    if (!load_native(&document, root, directory, profile, error, error_size) ||
        !load_lua(&document, root, directory, profile, error, error_size) ||
        !load_dex_patches(&document, root, profile, error, error_size) ||
        !load_path_sequence(&document, root, "assets", directory, profile, 0, error, error_size) ||
        !load_path_sequence(&document, root, "splits", directory, profile, 1, error, error_size) ||
        !load_signing(&document, root, directory, profile, error, error_size)) {
        goto cleanup;
    }
    ok = 1;

cleanup:
    if (document_ready) {
        yaml_document_delete(&document);
    }
    if (parser_ready) {
        yaml_parser_delete(&parser);
    }
    fclose(file);
    return ok;
}

static int supported_strategy(const char *strategy) {
    return strcmp(strategy, "auto") == 0 || strcmp(strategy, "provider") == 0 ||
           strcmp(strategy, "provider-debuggable") == 0;
}

int fpatch_profile_validate(const FpatchInjectProfile *profile,
                            char *error, size_t error_size) {
    size_t i;

    if (!profile->source[0]) {
        set_error(error, error_size, "No source APK was provided.");
        return 0;
    }
    if (!supported_strategy(profile->strategy)) {
        set_error(error, error_size,
                  "Unsupported strategy. Use auto, provider, or provider-debuggable.");
        return 0;
    }
    if (strcmp(profile->bootstrap_language, "java") != 0 &&
        strcmp(profile->bootstrap_language, "kotlin") != 0) {
        set_error(error, error_size, "bootstrap_language must be java or kotlin.");
        return 0;
    }
    for (i = 0; i < profile->native_count; i++) {
        if (!profile->native[i].path[0]) {
            set_error(error, error_size, "A native library has no path.");
            return 0;
        }
    }
    for (i = 0; i < profile->lua_count; i++) {
        if (!profile->lua[i].path[0]) {
            set_error(error, error_size, "A Lua script has no path.");
            return 0;
        }
    }
    if (profile->dex_patch_count > FPATCH_MAX_DEX_PATCHES) {
        set_error(error, error_size, "Too many declarative DEX patches.");
        return 0;
    }
    for (i = 0; i < profile->dex_patch_count; i++) {
        size_t j;
        const FpatchDexPatch *patch = &profile->dex_patches[i];
        if (!patch->target[0] || patch->action < FPATCH_DEX_PATCH_RETURN_TRUE ||
            patch->action > FPATCH_DEX_PATCH_REPLACE_STRING) {
            set_error(error, error_size, "A declarative DEX patch is incomplete.");
            return 0;
        }
        if (patch->action == FPATCH_DEX_PATCH_REPLACE_STRING &&
            (!patch->string_from[0] || !patch->string_to[0] ||
             strcmp(patch->string_from, patch->string_to) == 0 ||
             strlen(patch->string_from) != strlen(patch->string_to))) {
            set_error(error, error_size,
                      "replace_string requires distinct, equal-length from/to values.");
            return 0;
        }
        if (patch->action != FPATCH_DEX_PATCH_REPLACE_STRING &&
            (!patch->method[0] || patch->method[0] == '<')) {
            set_error(error, error_size, "A method DEX patch has an invalid selector.");
            return 0;
        }
        for (j = 0; j < i; j++) {
            const FpatchDexPatch *prior = &profile->dex_patches[j];
            if (patch->action != FPATCH_DEX_PATCH_REPLACE_STRING &&
                prior->action != FPATCH_DEX_PATCH_REPLACE_STRING &&
                strcmp(patch->target, prior->target) == 0 &&
                strcmp(patch->method, prior->method) == 0) {
                set_error(error, error_size,
                          "Duplicate target/method entries are not allowed in dex_patches.");
                return 0;
            }
            if (patch->action == FPATCH_DEX_PATCH_REPLACE_STRING &&
                prior->action == FPATCH_DEX_PATCH_REPLACE_STRING &&
                strcmp(patch->target, prior->target) == 0 &&
                strcmp(patch->string_from, prior->string_from) == 0) {
                set_error(error, error_size,
                          "Duplicate target/from entries are not allowed in dex_patches.");
                return 0;
            }
        }
    }
    return 1;
}
