# An editor and a broadcaster share the installation, but an exported game
# needs only the libraries linked by the player and its C++ runtime.
string(REPLACE "|" ";" files "${DLLS}")
set(names)
foreach(file IN LISTS files)
    if(file)
        get_filename_component(name "${file}" NAME)
        list(APPEND names "${name}")
    endif()
endforeach()
list(REMOVE_DUPLICATES names)
list(SORT names)
string(JOIN "\n" manifest ${names})
file(WRITE "${OUTPUT}" "${manifest}\n")
