"""Chunked-Transfer-Coding Example.

Hit this server with:
    curl http://localhost:8080/ > /dev/null

and you will see content immediately streaming from the server. It doesn't
sit and buffer anywhere (except in a small chunk-size buffer) even though
the length of the content is unknown when the response starts.

"""
import diesel
from diesel.web import DieselFlask, Response

app = DieselFlask(__name__)

def slow_thing():
    for i in xrange(100):
        yield "%s\n" % (str(i) * 1024)
        diesel.sleep(.1)

@app.route("/")
@app.chunk
def hello():
    response = Response(slow_thing(), mimetype='text/csv')
    return response

if __name__ == '__main__':
    def t():
        while True:
            diesel.sleep(1)
            print "also looping.."
    app.diesel_app.add_loop(diesel.Loop(t))
    # Very important - you will not get chunking if debug=True!
    app.run(debug=False)
