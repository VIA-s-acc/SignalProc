# before the header
pipeline "comments" v1  # header note

# above x
node x = samples(values="1 2 3 4", rate=8)  # tail x
node g = gain(
    # inside the parens
    db=-6)

# above the wire
x -> g  # wire tail
probe g as "g"  # probe tail
# the end
