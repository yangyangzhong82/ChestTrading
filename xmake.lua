add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")
add_repositories("yyz-repo https://github.com/yangyangzhong82/xmake-repo.git")

-- add_requires("levilamina x.x.x") for a specific version
-- add_requires("levilamina develop") to use develop version
-- please note that you should add bdslibrary yourself if using dev version
if is_config("target_type", "server") then
    add_requires("levilamina 1.9.2", {configs = {target_type = "server"}})
else
    add_requires("levilamina", {configs = {target_type = "client"}})
end

add_requires("levibuildscript")
add_requires("sqlite3")
add_requires("legacymoney")

add_requires("debug_shape")
add_requires("Bedrock-Authority 0.2.0", {optional = true})
if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

function configure_chest_target(target_name, with_permission_group)
target(target_name)
    set_default(target_name == "ChestTrading")
    add_rules("@levibuildscript/linkrule")

    local permission_dependency = ""
    if with_permission_group then
        permission_dependency = [[,
        {
            "name": "Bedrock-Authority"
        }]]
    end

    add_rules("@levibuildscript/modpacker", {
        pluginName           = "ChestTrading",
        modFile              = "ChestTrading.dll",
        permissionDependency = permission_dependency
    })
    add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    add_defines("NOMINMAX", "UNICODE")
    add_packages("levilamina", "sqlite3", "legacymoney", "debug_shape")
    if with_permission_group then
        add_packages("Bedrock-Authority")
        add_defines("CT_ENABLE_PERMISSION_GROUP=1")
    else
        add_defines("CT_ENABLE_PERMISSION_GROUP=0")
    end
    set_exceptions("none") -- To avoid conflicts with /EHa.
    set_kind("shared")
    set_languages("c++23")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end

    after_build(function(target)
        local outputdir = path.join(os.projectdir(), "bin", target:name())
        local langsrc   = path.join(os.projectdir(), "lang")
        local langdst   = path.join(outputdir, "lang")
        if os.isdir(langsrc) then
            os.mkdir(langdst)
            os.cp(path.join(langsrc, "**"), langdst)
        end

        -- Keep package output canonical to avoid loading stale variant-named binaries.
        if target:name() ~= "ChestTrading" then
            local variantDll = path.join(outputdir, target:name() .. ".dll")
            local variantPdb = path.join(outputdir, target:name() .. ".pdb")
            if os.isfile(variantDll) then
                os.rm(variantDll)
            end
            if os.isfile(variantPdb) then
                os.rm(variantPdb)
            end
        end
    end)
end

configure_chest_target("ChestTrading", true)
configure_chest_target("ChestTradingNoPerm", false)
