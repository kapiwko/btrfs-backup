var oldPanels = panels();
for (var index = 0; index < oldPanels.length; ++index) {
    oldPanels[index].remove();
}

var panel = new Panel;
panel.location = "bottom";
panel.alignment = "center";
panel.length = 620;
panel.height = 44;
panel.floating = false;
panel.addWidget("org.kde.plasma.panelspacer");

var widget = panel.addWidget("org.btrfsbackup.plasmoid");
widget.currentConfigGroup = ["General"];
widget.writeConfig("showStorage", false);
widget.globalShortcut = "Meta+B";
panel.addWidget("org.btrfsbackup.screenshotclock");
print(widget.id);
