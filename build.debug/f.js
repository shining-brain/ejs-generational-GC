function Node(value) {
    this.value = value;
    this.next = null;
    this.data = null;
}

print("=== Building long-lived object chains in Old space ===");

var chainHeads = [];
var chainLength = 2000;  //2000nodes per chain
var numChains = 100;     // 100 chains

for (var c = 0; c < numChains; c++) {
    var head = new Node(c * 1000);
    chainHeads[c] = head;
    
    var current = head;
    for (var i = 1; i < chainLength; i++) {
        var node = new Node(c * 1000 + i);
        current.next = node;
        current = node;
    }
}

print("Created " + numChains + " chains, each with " + chainLength + " nodes");
print("Total Old objects: " + (numChains * chainLength));

for (var i = 0; i < 10000; i++) {
    var temp = new Node(i);
}

for (var round = 0; round < 200; round++) {

    var youngData0 = new Node(round * 100);
    chainHeads[0].data = youngData0;  // Old->Young
    
    var youngData1 = new Node(round * 100 + 1);
    chainHeads[5].data = youngData1;  // Old->Young
    
    var youngData2 = new Node(round * 100 + 2);
    chainHeads[10].data = youngData2;  // Old->Young
    
    var youngData3 = new Node(round * 100 + 3);
    chainHeads[15].data = youngData3;  // Old->Young
    
    for (var j = 0; j < 10000; j++) {
        var garbage = new Node(j);
    }
    
}

// print("=== Verification ===");

// var totalNodes = 0;
// for (var c = 0; c < numChains; c++) {
//     var current = chainHeads[c];
//     var count = 0;
//     while (current) {
//         count++;
//         current = current.next;
//     }
//     totalNodes = totalNodes + count;
// }

// print("Total Old nodes alive: " + totalNodes);
print("=== Test complete ===");