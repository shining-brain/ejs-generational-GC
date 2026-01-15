function Item(val) {
    this.value = val;
    this.check = function() { return this.value; };
}


var persistent = new Item(101);

print("Generating garbage...");
for (var i = 0; i < 200000; i++) {
    var junk = new Item(i);
}

print("Verifying persistent object...");
var result = persistent.check();

if (result === 101) {
    print("SUCCESS: DRAM object valid.");
} else {
    print("FAILURE: Got " + result);
}