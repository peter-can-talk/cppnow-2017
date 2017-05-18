import collections
import json
import os
import re
import time

import flask

app = flask.Flask(__name__)

START_TIME = time.time()
HERE = os.path.dirname(os.path.realpath(__file__))
CODE = os.path.join(HERE, os.pardir, 'code')
CHOICES = [i for i in os.listdir(CODE) if not i.startswith('.')]
VOTES = [0 for _ in CHOICES]

Entry = collections.namedtuple('Entry', 'rank, name, votes')

@app.route('/')
def index():
    table = [(CHOICES[index], votes) for index, votes in enumerate(VOTES)]
    table.sort(key=lambda i: i[1], reverse=True)
    ranked = [Entry(rank + 1, *entry) for rank, entry in enumerate(table)]

    elapsed = round(time.time() - START_TIME, 2)
    return flask.render_template('index.html', votes=ranked, elapsed=elapsed)


@app.route('/vote/<indices>')
def vote(indices):
    chosen = []
    for index in re.split(r'\s*,\s*|\s+', indices):
        try:
            index = int(index)
            if not (0 <= index < len(VOTES)):
                raise ValueError
        except ValueError:
            return 'Invalid index: {0}'.format(index)
        VOTES[index] += 1
        chosen.append(CHOICES[index])
        print("Vote for '{0}' ({1})".format(CHOICES[index], index))

    return 'Thanks for voting. You chose: {0}'.format(', '.join(chosen))


def main():
    app.run('localhost', debug=True)

if __name__ == '__main__':
    main()
