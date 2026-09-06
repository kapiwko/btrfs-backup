set -l btrfs_backupctl_commands profile repository status installation runner restore target
complete -c btrfs-backupctl -n 'not __fish_seen_subcommand_from $btrfs_backupctl_commands' -a "$btrfs_backupctl_commands"
complete -c btrfs-backupctl -l status-root -r -d 'Override status root'
complete -c btrfs-backupctl -l history-root -r -d 'Override history root'
complete -c btrfs-backupctl -l profile-dir -r -d 'Override profile root'
complete -c btrfs-backupctl -l version -s V -d 'Show version'
complete -c btrfs-backupctl -l profile -r -d 'Select profile'
complete -c btrfs-backupctl -l help -s h -d 'Show help'
