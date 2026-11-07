pipeline "polyphase" v1

node x  = samples(values="4 6 10 12 8 6 5 5", rate=8)
node p  = polyphase()
node q  = ipolyphase()
node pr = check(kind=pr, tol=0, delay=0)

x -> p
p.even -> q.even
p.odd -> q.odd
q -> pr.in
x -> pr.ref
probe p.even as "чётные"
probe p.odd as "нечётные"
