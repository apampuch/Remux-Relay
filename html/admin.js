function send_command(command_obj) {

}

function play_pause() {
    const cmd = {};

    if (true) { // if playing
        cmd.command = "pause";    
    }
    else {
        cmd.command = "play";
    }

    send_command(cmd);
}

function seek() {
    
}