function Container() {
    this.data = [];
}

var containers = [];
for (var i = 0; i < 100; i++) {
    containers[i] = new Container();
}

for (var j = 0; j < 10000; j++) {
    var dummy = new Container();
}

print("=== Main loop: Old containers pointing to Young objects ===");
for (var round = 0; round < 50000; round++) {
    for (var i = 0; i < 100; i++) {
        containers[i].data = {value: round * 100 + i};
    }
}

print("=== Program end ===");