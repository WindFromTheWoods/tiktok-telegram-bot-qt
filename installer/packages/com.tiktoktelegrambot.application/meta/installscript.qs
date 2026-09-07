function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation(
            "CreateShortcut",
            "@TargetDir@/bin/TikTokTelegramBot.exe",
            "@StartMenuDir@/TikTok Telegram Bot.lnk",
            "workingDirectory=@TargetDir@/bin",
            "description=TikTok Telegram Bot");
    }
}
