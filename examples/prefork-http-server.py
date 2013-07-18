import diesel
from diesel.protocols.http import HttpServer, Response


def handle_http(req):
    body = "Hello, world"
    return Response(
        response=body,
        status=200,
        headers={'Content-Length':len(body)},
    )

if __name__ == '__main__':
    import sys
    usage = "Usage: python %s NUM_PROCESSES" % sys.argv[0]
    if len(sys.argv) != 2:
        print usage
        raise SystemExit(1)
    try:
        num_procs = int(sys.argv[1])
    except:
        print usage
        raise SystemExit(1)
    app = diesel.Application()
    app.add_service(diesel.Service(HttpServer(handle_http), 8000))
    app.run(processes=num_procs)
