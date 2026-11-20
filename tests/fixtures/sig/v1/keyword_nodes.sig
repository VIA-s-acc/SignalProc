pipeline "keywords" v1
node let = samples(values="1 2 3", rate=8)
node mark = gain(db=-6)
node out = gain(db=-6)
let -> mark
mark -> out
probe out as "out"
