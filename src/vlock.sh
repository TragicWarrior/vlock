#!%BOURNE_SHELL%
#
# vlock.sh -- start script for vlock, the VT locking program for linux
# 
# This program is copyright (C) 2007 Frank Benkstein, and is free
# software which is freely distributable under the terms of the
# GNU General Public License version 2, included as the file COPYING in this
# distribution.  It is NOT public domain software, and any
# redistribution not permitted by the GNU General Public License is
# expressly forbidden without prior written permission from
# the author.

# Ignore some signals.
trap : HUP INT QUIT TSTP

# Exit on error.
set -e

# Magic characters to clear the terminal.
CLEAR_SCREEN="`echo -e '\033[H\033[J'`"

# The lock messages (VLOCK_ENTER_PROMPT, VLOCK_ALL_MESSAGE and
# VLOCK_CURRENT_MESSAGE) are composed later by build_messages(), once the saver
# mode and wake key are known.  Pre-set any of them here or in ~/.vlockrc to
# override the defaults.

# Read user settings.
if [ -r "${HOME}/.vlockrc" ] ; then
  . "${HOME}/.vlockrc"
fi

# "Compile" time variables.
VLOCK_MAIN="%PREFIX%/sbin/vlock-main"
VLOCK_VERSION="%VLOCK_VERSION%"
# If set to "y" plugin support is enabled in vlock-main.
VLOCK_ENABLE_PLUGINS="%VLOCK_ENABLE_PLUGINS%"

# JSON configuration file, created with defaults on first run.
VLOCK_CONFIG_DIR="${XDG_CONFIG_HOME:-${HOME}/.config}/vlock"
VLOCK_CONFIG="${VLOCK_CONFIG_DIR}/config.json"

print_help() {
  echo >&2 "vlock: locks virtual consoles, saving your current session."
  if [ "${VLOCK_ENABLE_PLUGINS}" = "yes" ] ; then
    echo >&2 "Usage: vlock [options] [plugins...]"
  else
    echo >&2 "Usage: vlock [options]"
  fi
  echo >&2 "       Where [options] are any of:"
  echo >&2 "-c or --current: lock only this virtual console, allowing user to"
  echo >&2 "       switch to other virtual consoles."
  echo >&2 "-a or --all: lock all virtual consoles by preventing other users"
  echo >&2 "       from switching virtual consoles."
  if [ "${VLOCK_ENABLE_PLUGINS}" = "yes" ] ; then
    echo >&2 "-n or --new: allocate a new virtual console before locking,"
    echo >&2 "       implies --all."
    echo >&2 "-s or --disable-sysrq: disable SysRq while consoles are locked to"
    echo >&2 "       prevent killing vlock with SAK"
    echo >&2 "-t <seconds> or --timeout <seconds>: run screen saver plugins"
    echo >&2 "       after the given amount of time."
    echo >&2 "-S or --saver: start the screen saver plugins immediately instead"
    echo >&2 "       of waiting for [ESC] or the timeout."
  fi
  echo >&2 "-v or --version: Print the version number of vlock and exit."
  echo >&2 "-h or --help: Print this help message and exit."
  if [ "${VLOCK_ENABLE_PLUGINS}" = "yes" ] ; then
    echo >&2 ""
    echo >&2 "       [plugins...] are names of plugins to load.  Screen saver plugins"
    echo >&2 "       paint the screen after the --timeout (-t) elapses or when you"
    echo >&2 "       press [ESC]; the ones shipped by default are:"
    echo >&2 "         cmatrix   falling \"Matrix\" characters"
    echo >&2 "         train     an animated steam locomotive"
    echo >&2 "       The all, new and nosysrq plugins also have option equivalents"
    echo >&2 "       (-a, -n, -s).  Installed plugins live in %PREFIX%/lib/vlock/modules."
    echo >&2 ""
    echo >&2 "Examples:"
    echo >&2 "       vlock -a                  lock all consoles"
    echo >&2 "       vlock cmatrix             lock this console; Matrix saver on [ESC]"
    echo >&2 "       vlock -a -t 10 cmatrix    lock all; start Matrix after 10s idle"
    echo >&2 "       vlock -a -S cmatrix       lock all; start Matrix immediately"
    echo >&2 "       vlock -a train            lock all; steam-train saver"
    echo >&2 ""
    echo >&2 "       Set VLOCK_PLUGINS (in the environment or ~/.vlockrc) to load"
    echo >&2 "       plugins by default, e.g.  VLOCK_PLUGINS=\"cmatrix\""
  fi
  echo >&2 ""
  echo >&2 "Configuration is read from ${VLOCK_CONFIG}"
  echo >&2 "       (JSON; created on first run; needs jq; options override it)."
}

# Export variables only if they are set.  Some shells create an empty variable
# on export even if it was previously unset.
export_if_set() {
  while [ $# -gt 0 ] ; do
    if ( eval [ "\"\${$1+set}\"" = "set" ] ) ; then
      eval export $1
    fi
    shift
  done
}

# Apply a config value as a default: set and export NAME=VALUE unless NAME is
# already set, so command-line options and the environment win over the file.
set_default() {
  if eval "[ -z \"\${$1+x}\" ]" ; then
    eval "$1=\"\$2\""
  fi
  eval "export $1"
}

# Create the config file with basic defaults on first run, then map its
# "general" and per-module settings to VLOCK_* variables:
#   general.<key>          -> VLOCK_<KEY>
#   modules.<name>.<key>   -> VLOCK_<NAME>_<KEY>
# Requires jq; without it the file is ignored and only options/environment apply.
read_config() {
  command -v jq >/dev/null 2>&1 || return 0

  if [ ! -e "${VLOCK_CONFIG}" ] && mkdir -p "${VLOCK_CONFIG_DIR}" 2>/dev/null ; then
    cat > "${VLOCK_CONFIG}" <<'EOF'
{
  "general": {
    "wake_key": "any"
  },
  "modules": {
    "cmatrix": {
      "color": "green",
      "bold": 0
    },
    "train": {
      "random": false
    }
  }
}
EOF
  fi

  [ -r "${VLOCK_CONFIG}" ] || return 0

  if ! jq empty "${VLOCK_CONFIG}" >/dev/null 2>&1 ; then
    echo >&2 "vlock: warning: ${VLOCK_CONFIG} is not valid JSON; ignoring it"
    return 0
  fi

  eval "$(jq -r '
    ((.general // {}) | to_entries[]
      | select(.value | type | (. == "string" or . == "number" or . == "boolean"))
      | "set_default VLOCK_\(.key|ascii_upcase|gsub("[^A-Z0-9_]";"_")) \(.value|tostring|@sh)"),
    ((.modules // {}) | to_entries[] | select(.value | type == "object") | .key as $m
      | .value | to_entries[]
      | select(.value | type | (. == "string" or . == "number" or . == "boolean"))
      | "set_default VLOCK_\($m|ascii_upcase|gsub("[^A-Z0-9_]";"_"))_\(.key|ascii_upcase|gsub("[^A-Z0-9_]";"_")) \(.value|tostring|@sh)")
  ' "${VLOCK_CONFIG}")"
}

# Human-readable name of the configured wake key (VLOCK_WAKE_KEY), matching the
# keys that vlock-main actually accepts to dismiss the screen saver.
wake_key_label() {
  case "${VLOCK_WAKE_KEY:-any}" in
    enter|return) echo "[ENTER]" ;;
    space)        echo "the space bar" ;;
    backspace)    echo "[BACKSPACE]" ;;
    *)            echo "any key" ;;
  esac
}

# Compose the lock messages unless the user already supplied their own.  The
# unlock instruction depends on how the password prompt is reached: in immediate
# saver mode (-S) the saver is already on screen and the wake key brings up the
# prompt, so name that key; otherwise [ENTER] does.
build_messages() {
  if [ -z "${VLOCK_ENTER_PROMPT+set}" ] ; then
    case "${VLOCK_SAVER:-}" in
      1|y|Y|yes|true|on)
        VLOCK_ENTER_PROMPT="Please press $(wake_key_label) to unlock." ;;
      *)
        VLOCK_ENTER_PROMPT="Please press [ENTER] to unlock." ;;
    esac
  fi

  if [ -z "${VLOCK_ALL_MESSAGE+set}" ] ; then
    VLOCK_ALL_MESSAGE="${CLEAR_SCREEN}\
The entire console display is now completely locked.
You will not be able to switch to another virtual console.

${VLOCK_ENTER_PROMPT}"
  fi

  if [ -z "${VLOCK_CURRENT_MESSAGE+set}" ] ; then
    VLOCK_CURRENT_MESSAGE="${CLEAR_SCREEN}\
This TTY is now locked.

${VLOCK_ENTER_PROMPT}"
  fi
}

main() {
  short_options_with_arguments="t"
  long_options_with_arguments="timeout"

  # Parse command line arguments.
  while [ $# -gt 0 ] ; do
    case "$1" in
      -[!-]?*)
        # Strip "-" to get the list of option characters.
        options="${1#-}"
        shift

        last_option_argument="${options}"
        last_option_index=0

        # If an option character takes an argument all characters after it
        # become the argument if it isn't already the last one.  E.g. if "x"
        # takes an argument "-fooxbar" becomes "-foo -x bar".
        while [ -n "${last_option_argument}" ] ; do
          # Get first option character.
          option="$(expr "${last_option_argument}" : '\(.\)')"
          # Strip it from the list of option characters.
          last_option_argument="${last_option_argument#?}"
          last_option_index=$((${last_option_index} + 1))

          if expr "${short_options_with_arguments}" : "${option}" >/dev/null ; then
            # Prepend "-" plus option character and rest of option string to $@.
            set -- "-${option}" "${last_option_argument}" "$@"

            # Remove all characters after the option character.
            if [ "${last_option_index}" -gt 1 ] ; then
              options="$(expr "${options}" : "\(.\{$((${last_option_index}-1))\}\)")"
            else
              options=""
            fi

            break
          fi
        done

        # Convert clashed arguments like "-foobar" to "-f -o -o -b -a -r".
        while [ -n "${options}" ] ; do
          # Get last option character.
          option="$(expr "${options}" : '.*\(.\)')"
          # Strip it from the list of option characters.
          options="${options%?}"
          # Prepend "-" plus option character to $@.
          set -- "-${option}" "$@"
        done
      ;;
      --?*=?*)
        # Extract option name and argument.
        option="$(expr "x$1" : 'x--\([^=]*\)=.*')"
        option_argument="$(expr "x$1" : 'x--[^=]*=\(.*\)')"
        shift

        compare_options="${long_options_with_arguments}"

        # Find the option in the list of options that take an argument.
        while [ -n "${compare_options}" ] ; do
          compare_option="${compare_options%%,*}"
          compare_options="${compare_options#"${compare_option}"}"
          compare_options="${compare_options#,}"

          if [ "${option}" = "${compare_option}" ] ; then
            set -- "--${option}" "${option_argument}" "$@"
            unset option option_argument
            break
          fi
        done

        if [ -n "${option}" ] ; then
          echo >&2 "$0: option '--${option}' does not allow an argument"
          exit 1
        fi
      ;;
      -a|--all)
        plugins="${plugins} all"
        shift
        ;;
      -c|--current)
        unset plugins
        shift
        ;;
      -n|--new)
        plugins="${plugins} new"
        shift
        ;;
      -s|--disable-sysrq)
        plugins="${plugins} nosysrq"
        shift
        ;;
      -S|--saver)
        VLOCK_SAVER=y
        shift
        ;;
      -t|--timeout)
        VLOCK_TIMEOUT="$2"
        if ! shift 2 ; then
          echo >&2 "$0: option '$1' requires an argument"
          exit 1
        fi
        ;;
      -h|--help)
       print_help
       exit
       ;;
      -v|--version)
        if [ "${VLOCK_ENABLE_PLUGINS}" = "yes" ] ; then
          echo >&2 "vlock version ${VLOCK_VERSION}"
        else
          echo >&2 "vlock version ${VLOCK_VERSION} (no plugin support)"
        fi
        exit
        ;;
      -[!-]|--?*)
        echo >&1 "$0: unknown option '$1'"
        print_help
        exit 1
      ;;
      --)
        # End of option list.
        shift
        break
        ;;
      *)
        for argument ; do
          if [ "${argument}" = "--" ] ; then
            has_double_dash="yes"
            break
          fi
        done

        if [ -n "${has_double_dash}" ] ; then
          set -- "$@" "$1"
        else
          set -- "$@" -- "$1"
        fi

        shift
        ;;
    esac
  done

  # Apply the configuration file (options parsed above take precedence).
  read_config

  # Compose the lock messages now that the saver mode and wake key are known.
  build_messages

  # Export variables for vlock-main.
  export_if_set VLOCK_TIMEOUT VLOCK_PROMPT_TIMEOUT VLOCK_SAVER VLOCK_TRAIN_RANDOM
  export_if_set VLOCK_CMATRIX_COLOR VLOCK_CMATRIX_BOLD
  export_if_set VLOCK_MESSAGE VLOCK_ALL_MESSAGE VLOCK_CURRENT_MESSAGE

  if [ "${VLOCK_ENABLE_PLUGINS}" = "yes" ] ; then
    exec "${VLOCK_MAIN}" ${plugins} ${VLOCK_PLUGINS} "$@"
  else
    exec "${VLOCK_MAIN}" ${plugins}
  fi
}

main "$@"
