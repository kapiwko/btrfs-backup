var oldPanels = panels();
for (var index = 0; index < oldPanels.length; ++index) {
    oldPanels[index].remove();
}

var panel = new Panel;
panel.location = "bottom";
panel.alignment = "center";
panel.length = 1024;
panel.height = 44;
panel.floating = false;
panel.addWidget("org.kde.plasma.panelspacer");

var widget = panel.addWidget("org.kde.plasma.notifications");
widget.globalShortcut = "Meta+N";
panel.addWidget("org.kde.plasma.digitalclock");
print(widget.id);
