print("========== GC Reference Test ==========");


var oldObj1 = { name: "old1", value: 100 };
var oldObj2 = { name: "old2", value: 200 };
var oldArray = [1, 2, 3, 4, 5];

print("Created old objects during init phase");


function triggerGC() {
    var garbage = [];
    for (var i = 0; i < 5000; i++) {
        garbage.push({ x: i, y: i * 2 });
    }
    return garbage.length;
}

print("\n--- Triggering first GC ---");
triggerGC();


print("\n--- Verifying old objects after first GC ---");
print("oldObj1.name = " + oldObj1.name + " (expected: old1)");
print("oldObj1.value = " + oldObj1.value + " (expected: 100)");
print("oldObj2.name = " + oldObj2.name + " (expected: old2)");
print("oldObj2.value = " + oldObj2.value + " (expected: 200)");
print("oldArray[0] = " + oldArray[0] + " (expected: 1)");
print("oldArray[4] = " + oldArray[4] + " (expected: 5)");


print("\n--- Creating Young objects and linking from Old ---");
var youngObj1 = { name: "young1", value: 999 };
var youngObj2 = { name: "young2", value: 888 };
var youngArray = [10, 20, 30];

oldObj1.child = youngObj1;
oldObj2.child = youngObj2;
oldObj1.arr = youngArray;

print("Created Old->Young references (should trigger write barrier)");

print("\n--- Triggering second GC (with remembered set) ---");
triggerGC();

print("\n--- Verifying Old->Young references after GC ---");
print("oldObj1.child.name = " + oldObj1.child.name + " (expected: young1)");
print("oldObj1.child.value = " + oldObj1.child.value + " (expected: 999)");
print("oldObj2.child.name = " + oldObj2.child.name + " (expected: young2)");
print("oldObj2.child.value = " + oldObj2.child.value + " (expected: 888)");
print("oldObj1.arr[0] = " + oldObj1.arr[0] + " (expected: 10)");
print("oldObj1.arr[2] = " + oldObj1.arr[2] + " (expected: 30)");

print("\n--- Testing reference chains ---");
var chain1 = { level: 1 };
var chain2 = { level: 2, parent: chain1 };
var chain3 = { level: 3, parent: chain2 };
oldObj1.chain = chain3;

print("Triggering GC with reference chain...");
triggerGC();

print("oldObj1.chain.level = " + oldObj1.chain.level + " (expected: 3)");
print("oldObj1.chain.parent.level = " + oldObj1.chain.parent.level + " (expected: 2)");
print("oldObj1.chain.parent.parent.level = " + oldObj1.chain.parent.parent.level + " (expected: 1)");

print("\n--- Testing array with object references ---");
var objArray = [];
for (var i = 0; i < 10; i++) {
    objArray.push({ id: i, data: "item" + i });
}
oldObj2.items = objArray;

triggerGC();

print("Verifying array objects after GC:");
for (var i = 0; i < 10; i++) {
    var item = oldObj2.items[i];
    if (item.id !== i || item.data !== "item" + i) {
        print("ERROR: item[" + i + "] corrupted! id=" + item.id + " data=" + item.data);
    }
}
print("Array objects verified successfully");

print("\n--- Testing circular references ---");
var circA = { name: "circA" };
var circB = { name: "circB" };
circA.ref = circB;
circB.ref = circA;
oldObj1.circular = circA;

triggerGC();

print("circA.ref.name = " + oldObj1.circular.ref.name + " (expected: circB)");
print("circA.ref.ref.name = " + oldObj1.circular.ref.ref.name + " (expected: circA)");

print("\n========== Final Verification ==========");
var allPassed = true;

if (oldObj1.name !== "old1") { print("FAIL: oldObj1.name"); allPassed = false; }
if (oldObj1.value !== 100) { print("FAIL: oldObj1.value"); allPassed = false; }
if (oldObj1.child.name !== "young1") { print("FAIL: oldObj1.child.name"); allPassed = false; }
if (oldObj1.child.value !== 999) { print("FAIL: oldObj1.child.value"); allPassed = false; }
if (oldObj2.child.name !== "young2") { print("FAIL: oldObj2.child.name"); allPassed = false; }
if (oldObj1.arr[1] !== 20) { print("FAIL: oldObj1.arr[1]"); allPassed = false; }
if (oldObj1.chain.parent.parent.level !== 1) { print("FAIL: chain reference"); allPassed = false; }
if (oldObj1.circular.ref.ref.name !== "circA") { print("FAIL: circular reference"); allPassed = false; }

if (allPassed) {
    print("\n*** ALL TESTS PASSED! ***");
    print("GC correctly updates all references including:");
    print("  - Old object internal references");
    print("  - Old->Young references (remembered set)");
    print("  - Reference chains");
    print("  - Array object references");
    print("  - Circular references");
} else {
    print("\n*** SOME TESTS FAILED! ***");
}